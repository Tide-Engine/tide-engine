#include "tide/mesh.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/math.h"
#include "tide/net.h"
#include "tide/pack.h"
#include "list_data_cache.h"
#include "mesh_levels.h"

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
static const uint8_t *span(const tide_heap *heap, const uint64_t offset, const uint64_t bytes)
{
    if (!heap) return tide_pack_span(offset, bytes);
    if (!offset || offset > heap->used || bytes > heap->used - offset || offset >> TIDE_HEAP_PAGE_SHIFT >= heap->pages) return NULL;
    const tide_page *p = heap->page[offset >> TIDE_HEAP_PAGE_SHIFT];
    if (!p) return NULL;
    const uint64_t first = (uint64_t)p->first << TIDE_HEAP_PAGE_SHIFT;
    if (offset < first || offset + bytes > first + p->size) return NULL;
    return (const uint8_t *)tide_page_data(p) + (size_t)(offset - first);
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
// kept past its grid. Of two dimensions or three, with a size on each axis or
// none: what reads it says which it takes.
typedef struct grid_seen {
    tide_heap *heap; // NULL for a grid of the pack's (tide/pack.h): its chunks are in the scratch area's slots
    tide_grid_dir dir;
    const tide_grid_record *records;
} grid_seen;

static bool see_grid(const tide_grid g, const tide_grid_shape *s, grid_seen *v)
{
    const uint32_t dims = s->dims == 3u ? 3u : 2u;
    // A chunk is a page at the most, which is also what keeps its size in an int. Each shift first: three of any
    // size could add up to a small number
    if (!g.at || (s->dims != 2u && s->dims != 3u) || s->cell == 0u || s->shift[0] > TIDE_HEAP_PAGE_SHIFT
        || s->shift[1] > TIDE_HEAP_PAGE_SHIFT || s->shift[2] > TIDE_HEAP_PAGE_SHIFT
        || s->shift[0] + s->shift[1] + s->shift[2] > TIDE_HEAP_PAGE_SHIFT || (dims == 2u && s->shift[2] != 0u)
        || tide_grid_chunk_bytes(s) > 1u << TIDE_HEAP_PAGE_SHIFT) {
        return false;
    }
    // The confirmed world's too: only its pages' hashes are asked of it, which it keeps
    v->heap = (tide_heap *)(uintptr_t)tide_heap_read(tide_handle_where(g.at));
    if (!v->heap && !tide_pack_holds(g.at)) return false;
    const uint64_t at = tide_handle_offset(g.at);
    const uint8_t *block = span(v->heap, at, sizeof(tide_block) + sizeof(tide_grid_dir));
    if (!block) return false;
    memcpy(&v->dir, block + sizeof(tide_block), sizeof v->dir);
    const tide_grid_dir *dir = &v->dir;
    if (dir->dims != dims || dir->size[0] < 0 || dir->size[1] < 0 || dir->size[2] < 0 || dir->records > dir->room) return false;
    v->records = (const tide_grid_record *)(const void *)
        span(v->heap, at + sizeof(tide_block) + sizeof(tide_grid_dir), (uint64_t)dir->records * sizeof(tide_grid_record));
    return !dir->records || v->records;
}

static _Thread_local tide_target_pixels_fn target_pixels;

tide_target_pixels_fn tide_texture_target_pixels(const tide_target_pixels_fn resolve)
{
    const tide_target_pixels_fn previous = target_pixels;
    target_pixels = resolve;
    return previous;
}

tide_int2 tide_texture_size(const tide_texture_ref texture)
{
    if (texture.target.id) {
        const tide_render_target target = tide_render_target_resolve(texture.target);
        if (!target.id || target.id == TIDE_RENDER_INVALID || target.id == TIDE_RENDER_WINDOW || tide_render_target_cubic(target.id)
            || (texture.image.height != TIDE_TEXTURE_DEPTH
                && tide_render_target_image(target.id) >= tide_render_target_colors(target.id))) return tide_i2(0, 0);
        return target_pixels ? target_pixels(target.size) : tide_i2(0, 0);
    }
    if (texture.image.height == TIDE_TEXTURE_DEPTH) return tide_i2(0, 0);
    if (texture.image.height == TIDE_TEXTURE_GRID) {
        tide_grid grid;
        tide_grid_shape shape;
        grid_seen seen;
        if (!tide_texture_grid(texture, &grid, &shape) || !see_grid(grid, &shape, &seen)
            || seen.dir.size[0] <= 0 || seen.dir.size[1] <= 0
            || seen.dir.size[0] > TIDE_GRID_TEXTURE_MOST || seen.dir.size[1] > TIDE_GRID_TEXTURE_MOST) return tide_i2(0, 0);
        return tide_i2(seen.dir.size[0], seen.dir.size[1]);
    }
    tide_texture image = texture.image;
    if (!image.pixels && !tide_pack_texture_find(image.version & TIDE_TEXTURE_NAME, &image)) return tide_i2(0, 0);
    return image.width > 0 && image.height > 0 ? tide_i2(image.width, image.height) : tide_i2(0, 0);
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

static void float_cells(const cells_writer *w, const uint8_t *cells, const int32_t count, uint8_t *out)
{
    (void)w;
    const float *row = (const float *)(const void *)cells;
    for (int32_t x = 0; x < count; x++, out += 2) {
        const uint16_t half = tide_half_from_float(row[x]);
        memcpy(out, &half, sizeof half);
    }
}

static void number_cells(const cells_writer *w, const uint8_t *cells, const int32_t count, uint8_t *out)
{
    for (int32_t x = 0; x < count; x++) w->write(cells + (size_t)x * w->cell, (int32_t *)(void *)(out + (size_t)x * w->bytes));
}

// The cells of a grid that its texture holds, and where: a box of them, from
// `lo`, `extent` along each axis (one layer, for a grid of two dimensions),
// whose layers are side by side, `across` of them, and then under each other.
// A row of a layer's cells is `wide` bytes of a row of the texture: layer z's
// rows start (z % across) * wide bytes along, (z / across) * extent[1] rows
// down. `lo` is where a chunk starts, on each axis.
typedef struct cells_box {
    uint32_t dims;
    int64_t lo[3], extent[3];
    int64_t across;
    size_t wide;
} cells_box;

// The cells of the chunk at `chunk` (in chunks) that are in the box: from[a]
// up to to[a] of its own along each axis, and in first[a], where its first
// cell is in the box. False where none is.
static bool chunk_in_box(const cells_box *box, const tide_grid_shape *s, const int64_t chunk[3], int64_t from[3], int64_t to[3],
                         int64_t first[3])
{
    bool some = true;
    for (uint32_t a = 0; a < 3u; a++) {
        const int64_t side = (int64_t)1 << s->shift[a];
        first[a] = (a < box->dims ? chunk[a] * side : 0) - box->lo[a];
        from[a] = first[a] < 0 ? -first[a] : 0;
        to[a] = first[a] + side > box->extent[a] ? box->extent[a] - first[a] : side;
        some &= from[a] < to[a];
    }
    return some;
}

enum { GRID_PARTS_MOST = 1 << 20 };

// A grid's cells into its texture's pixels, zeros where it has no chunk: all
// of them, or where the pixels are `kept`, the chunks whose hashes aren't the
// ones the texture was last written from, each row of them where it differs.
static void grid_cells(tide_draw_list *d, const uint32_t slot, uint8_t *pixels, bool kept, const grid_seen *v,
                       const tide_grid_shape *s, const cells_writer *w, const cells_box *box)
{
    const int32_t width = d->textures[slot - 1u].width, height = d->textures[slot - 1u].height;
    const size_t row = (size_t)width * tide_texels_bytes(d->textures[slot - 1u].format);
    // The chunks the box takes in, along each axis: it starts where one does
    int64_t start[3], count[3];
    uint64_t places = 1;
    for (uint32_t a = 0; a < 3u; a++) {
        const int64_t side = (int64_t)1 << s->shift[a];
        start[a] = box->lo[a] / side;
        count[a] = (box->extent[a] + side - 1) / side;
        places = places <= GRID_PARTS_MOST ? places * (uint64_t)count[a] : places;
    }
    // Each chunk's hash by its place among those, as the texture was last written, and after them, as the grid is now
    uint64_t *parts = v->heap && places <= GRID_PARTS_MOST ? tide_draw_texture_parts(d, slot, (uint32_t)places * 2u) : NULL;
    uint64_t *now = parts ? parts + places : NULL;
    uint64_t *chunks = tide_draw_texture_used(d, slot); // How many it was written from
    if (now) memset(now, 0, (size_t)places * sizeof *now);
    if (kept && !parts) { // Nothing says which chunks changed, or went: all of it again
        kept = false;
        tide_draw_texture_changed(d, slot, 0, 0, width, height);
    }
    if (!kept) memset(pixels, 0, row * (size_t)height);
    const uint64_t mark = tide_scratch_mark();
    uint8_t *made = kept ? tide_scratch_memory(((size_t)1 << s->shift[0]) * w->bytes) : NULL;
    uint64_t found = 0, had = 0;
    for (uint32_t r = 0; r < v->dir.records; r++) {
        const tide_grid_record *record = &v->records[r];
        const uint8_t *cells = span(v->heap, record->block, tide_grid_chunk_bytes(s));
        if (!cells || (box->dims == 2u && record->chunk[2] != 0)) continue;
        const int64_t chunk[3] = {record->chunk[0], record->chunk[1], box->dims == 3u ? record->chunk[2] : 0};
        int64_t from[3], to[3], first[3];
        if (!chunk_in_box(box, s, chunk, from, to, first)) continue;
        if (now) {
            const uint64_t place = (uint64_t)(((chunk[2] - start[2]) * count[1] + (chunk[1] - start[1])) * count[0]
                                              + (chunk[0] - start[0]));
            found += !now[place];
            had += !now[place] && parts[place];
            now[place] = chunk_hash(v, s, record);
            if (kept && parts[place] == now[place]) continue; // As it was
        }
        const int32_t cells_across = (int32_t)(to[0] - from[0]);
        const size_t run = (size_t)cells_across * w->bytes;
        for (int64_t z = from[2]; z < to[2]; z++) {
            const int64_t layer = first[2] + z;
            const size_t at = (size_t)(layer % box->across) * box->wide + (size_t)(first[0] + from[0]) * w->bytes;
            const int64_t top = layer / box->across * box->extent[1] + first[1];
            size_t left = (size_t)cells_across, right = 0;
            int64_t above = to[1], below = from[1];
            for (int64_t y = from[1]; y < to[1]; y++) {
                uint8_t *out = pixels + (size_t)(top + y) * row + at;
                const uint8_t *in = cells + (size_t)((z << (s->shift[0] + s->shift[1])) + (y << s->shift[0]) + from[0]) * s->cell;
                if (!kept) {
                    w->row(w, in, cells_across, out);
                    continue;
                }
                memset(made, 0, run);
                w->row(w, in, cells_across, made);
                size_t low, high;
                if (!differs(out, made, (size_t)cells_across, w->bytes, &low, &high)) continue;
                memcpy(out + low * w->bytes, made + low * w->bytes, (high - low) * w->bytes);
                if (low < left) left = low;
                if (high > right) right = high;
                if (y < above) above = y;
                below = y + 1;
            }
            if (right > left) {
                changed_bytes(d, slot, at + left * w->bytes, at + right * w->bytes, (int32_t)(top + above), (int32_t)(top + below));
            }
        }
    }
    // A chunk that went is zeros again
    for (uint64_t place = 0; kept && had < *chunks && place < places; place++) {
        if (!parts[place] || now[place]) continue;
        const int64_t chunk[3] = {start[0] + (int64_t)(place % (uint64_t)count[0]),
                                  start[1] + (int64_t)(place / (uint64_t)count[0] % (uint64_t)count[1]),
                                  start[2] + (int64_t)(place / (uint64_t)count[0] / (uint64_t)count[1])};
        int64_t from[3], to[3], first[3];
        if (!chunk_in_box(box, s, chunk, from, to, first)) continue;
        const size_t run = (size_t)(to[0] - from[0]) * w->bytes;
        for (int64_t z = from[2]; z < to[2]; z++) {
            const int64_t layer = first[2] + z;
            const size_t at = (size_t)(layer % box->across) * box->wide + (size_t)(first[0] + from[0]) * w->bytes;
            const int64_t top = layer / box->across * box->extent[1] + first[1];
            for (int64_t y = from[1]; y < to[1]; y++) memset(pixels + (size_t)(top + y) * row + at, 0, run);
            changed_bytes(d, slot, at, at + run, (int32_t)(top + from[1]), (int32_t)(top + to[1]));
        }
    }
    if (parts) memcpy(parts, now, (size_t)places * sizeof *parts);
    if (chunks) *chunks = found;
    tide_scratch_reset(mark);
}

#define KIND_COLORS UINT64_C(0x47434F4C)
#define KIND_FLOATS UINT64_C(0x47464C54)
#define KIND_RAW UINT64_C(0x47524157)

static uint32_t grid_texture(tide_draw_list *d, const tide_grid g, const tide_grid_shape *s, const bool raw)
{
    const bool colors = s->cell == sizeof(tide_color);
    grid_seen v;
    if (s->dims != 2u || (!colors && s->cell != sizeof(float)) || !see_grid(g, s, &v)) return 0;
    const int32_t width = v.dir.size[0], height = v.dir.size[1];
    // A grid with no size on an axis is no texture. No GPU has a texture bigger, and bytes that aren't a grid's
    // could say any size
    if (width <= 0 || height <= 0 || width > TIDE_GRID_TEXTURE_MOST || height > TIDE_GRID_TEXTURE_MOST) return 0;
    // A grid with a size on both axes has as many chunks as cover it, and no more of them than that
    const uint64_t across = ((uint64_t)width + (1u << s->shift[0]) - 1u) >> s->shift[0];
    const uint64_t down = ((uint64_t)height + (1u << s->shift[1]) - 1u) >> s->shift[1];
    if (v.dir.records > across * down) return 0;
    if (v.dir.flat && ((uint64_t)v.dir.chunks[0] != across || (uint64_t)v.dir.chunks[1] != down)) return 0;
    // A grid of the pack's has its file's version, in its directory's header, as the builder put it
    uint64_t version = v.heap ? tide_hash_end(grid_hash(&v, s)) : tide_pack_grid_version(g);
    if (raw) version ^= UINT64_C(0x5241575241575241); // Other bytes than the same cells as colors
    const cells_writer w = {.row = colors ? color_cells : float_cells, .bytes = colors ? 4u : 2u, .raw = raw};
    const cells_box box = {.dims = 2u, .extent = {width, height, 1}, .across = 1, .wide = (size_t)width * w.bytes};
    uint8_t *pixels;
    bool kept;
    const uint32_t slot = tide_draw_texture_edit(d, TIDE_PIXELS_GRID, g.at, version, width, height,
                                                 colors ? TIDE_TEXELS_BYTES : TIDE_TEXELS_HALF,
                                                 !colors ? KIND_FLOATS : raw ? KIND_RAW : KIND_COLORS, &pixels, &kept);
    if (pixels) grid_cells(d, slot, pixels, kept, &v, s, &w, &box);
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

static uint64_t take_mesh_key(tide_draw_list *d, tide_list vertices, tide_list indices);

static void mesh_lists(tide_draw_list *d, const tide_list vertices, const tide_list indices, const uint32_t slot,
                       const tide_filter filter, const tide_wrap wrap)
{
    (void)take_mesh_key(d, vertices, indices); // Copied as they are, with no version
    const int32_t vertex_count = tide_list_count(vertices);
    const int32_t index_count = tide_list_count(indices);
    if (vertex_count <= 0 || index_count < 3) return;
    // A list's elements side by side, as they are unless it's a world's big one
    bool vertices_copied, indices_copied;
    tide_vertex *v = tide_list_flatten(vertices, sizeof(tide_vertex), false, &vertices_copied);
    uint32_t *i = tide_list_flatten(indices, sizeof(uint32_t), false, &indices_copied);
    tide_draw_mesh_with(d, v, (uint32_t)vertex_count, i, (uint32_t)index_count, slot, filter, wrap); // A quad as one
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
    if (tide_handle_where(l.at) == TIDE_IN_SCRATCH) {
        // A mesh of the pack's says what's in it (tide_pack_mesh): its file's hash, with no vertex read
        uint64_t packed;
        if (tide_pack_list_version(l, size, &packed)) return tide_hash_end(tide_hash_more(h, &packed, sizeof packed));
        return tide_hash_end(tide_hash_more(h, tide_list_at(l, 0, size), (size_t)count * size));
    }
    tide_heap *heap = (tide_heap *)(uintptr_t)tide_heap_read(tide_handle_where(l.at));
    if (!heap) return tide_hash_end(h);
    const uint64_t block = tide_handle_offset(l.at);
    const tide_block *b = tide_heap_block(heap, block);
    if (!(b->b & TIDE_LIST_CHUNKED)) {
        const uint64_t page = tide_heap_block_hash(heap, block);
        return tide_hash_end(tide_hash_more(h, &page, sizeof page));
    }
    // In chunks, a page each: where they are, and what's in each
    const uint32_t chunks = b->b & ~TIDE_LIST_CHUNKED;
    const uint64_t *at = (const uint64_t *)(const void *)(b + 1);
    h = tide_hash_more(h, at, (size_t)chunks * sizeof(uint64_t));
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
    tide_heap *heap = tide_handle_where(list.at) == TIDE_IN_SCRATCH ? NULL : (tide_heap *)(uintptr_t)tide_heap_read(tide_handle_where(list.at));
    const tide_block *b = tide_block_at(list.at);
    const bool chunked = heap && b && (b->b & TIDE_LIST_CHUNKED) && size <= TIDE_LIST_CHUNK;
    const uint32_t per = chunked ? TIDE_LIST_CHUNK / size : elements;
    const uint32_t runs = (elements + per - 1u) / per;
    const uint64_t *chunks = chunked ? (const uint64_t *)(const void *)(b + 1) : NULL;
    // A chunk's hash is kept only while each write here keeps it: a list with no chunks keeps none, so that one
    // that comes to have chunks again finds none from before
    uint64_t *parts = tide_draw_texture_parts(d, slot, chunked ? runs : 0u);
    const uint64_t mark = tide_scratch_mark();
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
// What a world's list, map or grid of numbers is to the draw list: its place
// and how its elements are read, mixed into 63 bits. The top bit is a list
// made along the way's (UINT64_C(1) << 63 with its version).
static uint64_t numbers_id(const tide_handle at, const uint32_t layout)
{
    uint64_t h = tide_hash_more(TIDE_HASH_START, &at, sizeof at);
    h = tide_hash_end(tide_hash_more(h, &layout, sizeof layout));
    return h & ~(UINT64_C(1) << 63);
}

// Only a current ordinary flat heap page supplies a cheap mutation witness.
// moves alone is insufficient: writes into unique pages merely dirty hashed.
// Scratch offsets can be reused or written without any revision; chunked and
// immutable asset-backed lists keep the existing general path too.
static bool list_data_page(const tide_list list, tide_list_data_entry *seen)
{
    if (!list.at || tide_handle_where(list.at) == TIDE_IN_SCRATCH) return false;
    const tide_heap *heap = tide_heap_read(tide_handle_where(list.at));
    const uint64_t offset = tide_handle_offset(list.at);
    if (!heap || (offset >> TIDE_HEAP_PAGE_SHIFT) >= heap->pages
        || offset > heap->used || sizeof(tide_block) > heap->used - offset) return false;
    tide_page *const *pages = __atomic_load_n(&heap->page, __ATOMIC_ACQUIRE);
    const tide_page *page = __atomic_load_n(&pages[offset >> TIDE_HEAP_PAGE_SHIFT], __ATOMIC_ACQUIRE);
    if (!page || page->hashed == UINT32_MAX) return false;
    const uint64_t first = (uint64_t)page->first << TIDE_HEAP_PAGE_SHIFT;
    if (offset < first || offset - first > page->size || sizeof(tide_block) > page->size - (offset - first)) return false;
    const tide_block *block = (const tide_block *)(const void *)((const uint8_t *)tide_page_data(page)
        + (size_t)(offset - first));
    if (block->b & (TIDE_LIST_CHUNKED | TIDE_LIST_ASSET)) return false;
    seen->heap = (uintptr_t)heap;
    seen->heap_used = heap->used;
    seen->page_hash = page->hash;
    seen->hashed = page->hashed;
    return true;
}

static bool list_data_same_page(const tide_list_data_entry *a, const tide_list_data_entry *b)
{
    return a->heap == b->heap && a->heap_used == b->heap_used
        && a->page_hash == b->page_hash && a->hashed == b->hashed;
}

static uint32_t list_data_cached(tide_draw_list *d, const tide_list list, const uint32_t element_size,
                                const uint32_t words, const uint32_t layout, tide_list_words_fn *write,
                                const int32_t held, int32_t *count)
{
    if (!d->list_data) return 0;
    tide_list_data_entry seen = {0};
    if (!list_data_page(list, &seen)) return 0;
    for (uint32_t i = 0; i < TIDE_LIST_DATA_CACHE_ENTRIES; i++) {
        const tide_list_data_entry *entry = &d->list_data->entries[i];
        if (entry->at != list.at || entry->element_size != element_size || entry->words != words
            || entry->layout != layout || entry->write != write || entry->held != held
            || !entry->slot || entry->slot > d->texture_count || !list_data_same_page(entry, &seen)) continue;
        tide_draw_texture *texture = &d->textures[entry->slot - 1u];
        if (texture->space != TIDE_PIXELS_LIST || texture->id != entry->id || texture->version != entry->version
            || texture->format != TIDE_TEXELS_INTS || texture->width != entry->width || texture->height != entry->height
            || !texture->changes || texture->changes->kind != entry->kind) continue;
        // Observing a valid hash must invalidate retained writable list/grid
        // addresses, just as tide_heap_block_hash does on the general path.
        tide_heap *heap = (tide_heap *)seen.heap;
        __atomic_add_fetch(&heap->moves, 1u, __ATOMIC_RELAXED);
        texture->frame = d->frame;
        *count = entry->count;
        return entry->slot;
    }
    return 0;
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
    const uint32_t cached = list_data_cached(d, list, element_size, words, layout, write, held, count);
    if (cached) return cached;
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
    const uint64_t id = tide_handle_where(list.at) == TIDE_IN_SCRATCH ? UINT64_C(1) << 63 | version >> 1 : numbers_id(list.at, layout);
    tide_list_data_entry seen = {0};
    const bool cacheable = list_data_page(list, &seen);
    uint8_t *out;
    bool kept;
    const uint32_t slot = tide_draw_texture_edit(d, TIDE_PIXELS_LIST, id, version, width, height, TIDE_TEXELS_INTS, kind,
                                                 &out, &kept);
    if (out) list_numbers(d, slot, (int32_t *)(void *)out, kept, list, element_size, elements, stride, write);
    if (cacheable && slot) {
        if (!d->list_data) d->list_data = tide_alloc_zeroed(1, sizeof *d->list_data);
        tide_list_data_entry *entry = &d->list_data->entries[d->list_data->next++ % TIDE_LIST_DATA_CACHE_ENTRIES];
        *entry = seen;
        entry->at = list.at;
        entry->element_size = element_size;
        entry->words = words;
        entry->layout = layout;
        entry->write = write;
        entry->held = held;
        entry->count = (int32_t)elements;
        entry->width = width;
        entry->height = height;
        entry->slot = slot;
        entry->id = id;
        entry->version = version;
        entry->kind = kind;
    }
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
    if (map.live == 0 || held <= 0 || words == 0 || key_word >= words || entry_size < sizeof(int64_t)) return 0;
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
    const uint64_t id = tide_handle_where(map.entries.at) == TIDE_IN_SCRATCH ? UINT64_C(1) << 63 | version >> 1
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
        const uint64_t mark = tide_scratch_mark();
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
                             const uint32_t layout, tide_list_words_fn *write, int32_t *info)
{
    const uint32_t dims = s->dims == 3u ? 3u : 2u;
    memset(info, 0, (dims == 3u ? TIDE_GRID3_DATA_INTS : TIDE_GRID2_DATA_INTS) * sizeof *info);
    grid_seen v; // As a grid that's sampled is read (grid_texture): nothing a handle names is trusted
    if (!see_grid(g, s, &v)) return 0;
    for (uint32_t a = 0; a < dims; a++) info[a] = v.dir.size[a]; // Its size is its own, whatever of it is sent
    if (words == 0) return 0; // Nothing is read of its cells: only its size is asked
    int64_t lo[3], extent[3];
    if (!data_box(&v.dir, v.records, s, lo, extent)) return 0; // Nothing in it: zeros everywhere
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
    // Its cells as these numbers, laid out so, and no list's: a box that starts elsewhere, or has its layers
    // otherwise, is other pixels, of which nothing is kept
    const int64_t shape[8] = {s->cell, words, 0x47524944, lo[0], lo[1], lo[2], across, layout};
    const uint64_t kind = tide_hash(shape, sizeof shape);
    const uint64_t cells = v.heap ? grid_hash(&v, s) : tide_pack_grid_version(g);
    const uint64_t version = tide_hash_end(tide_hash_more(cells, &kind, sizeof kind));
    const cells_writer w = {.row = number_cells, .bytes = stride * 4u, .write = write, .cell = s->cell};
    const cells_box box = {.dims = dims, .lo = {lo[0], lo[1], lo[2]}, .extent = {extent[0], extent[1], extent[2]},
                           .across = across, .wide = (size_t)wide * 16u};
    uint8_t *out;
    bool kept;
    const uint32_t slot = tide_draw_texture_edit(d, TIDE_PIXELS_LIST, numbers_id(g.at, layout), version, (int32_t)width,
                                                 (int32_t)height, TIDE_TEXELS_INTS, kind, &out, &kept);
    if (out) grid_cells(d, slot, out, kept, &v, s, &w, &box);
    for (uint32_t a = 0; a < dims; a++) {
        info[dims + a] = (int32_t)lo[a];
        info[2u * dims + a] = (int32_t)extent[a];
    }
    if (dims == 3u) info[9] = (int32_t)across;
    return slot;
}

// The levels of detail of a mesh the list just copied, where its indices are a mesh's of the pack that was
// built with them (an asset marked [LOD]): the draws after it pick among them.
static void mesh_levels(tide_draw_list *d, const uint32_t mesh, const tide_list indices)
{
    tide_mesh_levels levels;
    if (tide_handle_where(indices.at) == TIDE_IN_SCRATCH && tide_pack_list_levels(indices, &levels)) tide_draw_mesh_levels(d, mesh, &levels);
}

uint64_t tide_mesh_key(const tide_list vertices, const uint32_t vertex_size, const tide_list indices)
{
    // Counts and sizes first, so two splits of the same bytes are other meshes
    const int32_t vertex_count = tide_list_count(vertices);
    const int32_t index_count = tide_list_count(indices);
    const int64_t shape[3] = {vertex_count, index_count, vertex_size};
    uint64_t h = tide_hash_more(TIDE_HASH_START, shape, sizeof shape);
    bool copied;
    if (vertex_count > 0) {
        void *v = tide_list_flatten(vertices, vertex_size, false, &copied);
        h = tide_hash_more(h, v, (size_t)vertex_count * vertex_size);
        tide_list_unflatten(vertices, v, vertex_size, false, copied);
    }
    if (index_count > 0) {
        void *i = tide_list_flatten(indices, sizeof(uint32_t), false, &copied);
        h = tide_hash_more(h, i, (size_t)index_count * sizeof(uint32_t));
        tide_list_unflatten(indices, i, sizeof(uint32_t), false, copied);
    }
    return tide_hash_end(h) | UINT64_C(1) << 63;
}

void tide_draw_mesh_key(tide_draw_list *d, const tide_mesh_lists lists, const uint64_t key)
{
    const bool keyed = (key >> 63) != 0;
    d->mesh_key = keyed ? key : 0;
    d->mesh_key_vertices = keyed ? lists.vertices.at : 0;
    d->mesh_key_indices = keyed ? lists.indices.at : 0;
}

// The key the draw list was told these two lists hold (tide_draw_mesh_key), or 0: whichever lists they are, it's
// told no more
static uint64_t take_mesh_key(tide_draw_list *d, const tide_list vertices, const tide_list indices)
{
    const uint64_t key = d->mesh_key && d->mesh_key_vertices == vertices.at && d->mesh_key_indices == indices.at ? d->mesh_key : 0;
    d->mesh_key = d->mesh_key_vertices = d->mesh_key_indices = 0;
    return key;
}

// A mesh the game made, by its key: one mesh wherever its copies are, as nothing writes their lists. No list's
// place is this (a handle's offset is under 2^62)
#define MESH_KEYED UINT64_MAX
static void note_relisted(tide_draw_list *d, uint32_t mesh, tide_list vertices, tide_list indices, uint32_t size, uint64_t id,
                          uint64_t other);

static uint32_t mesh3_slot(tide_draw_list *d, const tide_list vertices, const tide_list indices)
{
    const uint64_t key = take_mesh_key(d, vertices, indices);
    const int32_t vertex_count = tide_list_count(vertices);
    const int32_t index_count = tide_list_count(indices);
    if (vertex_count <= 0 || index_count < 3) return 0;
    // A mesh is its two lists at their versions. A list made along the way
    // is what's in it, wherever it is in the scratch area: a view that runs
    // for each entity makes `[0, 1, 2]` in another place each time, and its
    // instances are still one mesh's
    const uint64_t scratch = tide_handle_of(TIDE_IN_SCRATCH, 0); // No world's list's place
    uint64_t version = key, id = MESH_KEYED, other = MESH_KEYED;
    if (!key) {
        version = list_version(vertices, sizeof(tide_vertex3));
        const uint64_t index_version = list_version(indices, sizeof(uint32_t));
        version = tide_hash_end(tide_hash_more(version, &index_version, sizeof index_version));
        id = tide_handle_where(vertices.at) == TIDE_IN_SCRATCH ? scratch : vertices.at;
        other = tide_handle_where(indices.at) == TIDE_IN_SCRATCH ? scratch : indices.at;
    }
    version = tide_draw_lod_version(d, version);
    bool fill;
    const uint32_t mesh = tide_draw_mesh_slot(d, TIDE_MESH_LISTS, id, other, version, &fill);
    if (fill) {
        bool vertices_copied, indices_copied;
        tide_vertex3 *v = tide_list_flatten(vertices, sizeof(tide_vertex3), false, &vertices_copied);
        uint32_t *i = tide_list_flatten(indices, sizeof(uint32_t), false, &indices_copied);
        tide_draw_mesh_fill(d, mesh, v, (uint32_t)vertex_count, i, (uint32_t)index_count);
        tide_list_unflatten(indices, i, sizeof(uint32_t), false, indices_copied);
        tide_list_unflatten(vertices, v, sizeof(tide_vertex3), false, vertices_copied);
        mesh_levels(d, mesh, indices);
        tide_draw_lod_request(d, mesh);
        note_relisted(d, mesh, vertices, indices, d->meshes[mesh - 1u].vertex_size, id, other);
    }
    if (d->diagnostics) {
        d->meshes[mesh - 1u].profile_vertices = vertices.at;
        d->meshes[mesh - 1u].profile_indices = indices.at;
    }
    return mesh;
}

void tide_draw_mesh_shaded(tide_draw_list *d, const tide_shader *shader, const tide_list vertices, const tide_list indices,
                           const void *instance, const void *uniforms, const uint32_t *textures, const uint64_t sampling)
{
    tide_draw_mesh_shaded_instances(d, shader, vertices, indices, instance, 1, uniforms, textures, sampling);
}

// A mesh just copied from a world's two lists (not lists made along the way, nor a made mesh's key): what a
// later read of them again checks (tide_draw_mesh_relist), and that its copy may go while the renderer has it.
static void note_relisted(tide_draw_list *d, const uint32_t mesh, const tide_list vertices, const tide_list indices,
                          const uint32_t size, const uint64_t id, const uint64_t other)
{
    const uint64_t scratch = tide_handle_of(TIDE_IN_SCRATCH, 0);
    tide_draw_mesh_data *m = &d->meshes[mesh - 1u];
    if (id == MESH_KEYED || other == MESH_KEYED || id == scratch || other == scratch || !m->vertex_count) return;
    m->vertices_version = list_version(vertices, size);
    m->indices_version = list_version(indices, sizeof(uint32_t));
    m->relisted = true;
}

void tide_draw_mesh_relist(tide_draw_list *d, const uint32_t slot, const tide_draw_mesh_data *m)
{
    const tide_list vertices = {m->id}, indices = {m->other};
    const uint32_t size = m->space == TIDE_MESH_BYTES ? m->vertex_size : (uint32_t)sizeof(tide_vertex3);
    // Still at the versions it was copied at (they were, as the frame drew it), or it draws nothing
    if (!m->relisted || !size || !tide_heap_read(tide_handle_where(vertices.at)) || !tide_heap_read(tide_handle_where(indices.at))
        || tide_list_count(vertices) != (int32_t)m->vertex_count || list_version(vertices, size) != m->vertices_version
        || list_version(indices, sizeof(uint32_t)) != m->indices_version) return;
    const int32_t index_count = tide_list_count(indices);
    if (index_count < 3) return;
    bool vertices_copied, indices_copied;
    void *v = tide_list_flatten(vertices, size, false, &vertices_copied);
    uint32_t *i = tide_list_flatten(indices, sizeof(uint32_t), false, &indices_copied);
    tide_draw_mesh_fill_bytes(d, slot, v, size, m->vertex_count, i, (uint32_t)index_count);
    tide_list_unflatten(indices, i, sizeof(uint32_t), false, indices_copied);
    tide_list_unflatten(vertices, v, size, false, vertices_copied);
    tide_draw_mesh_data *to = &d->meshes[slot - 1u];
    to->relisted = true;
    to->vertices_version = m->vertices_version;
    to->indices_version = m->indices_version;
}

// The mesh of a shader's draw of two lists, 0 for none: found by their versions, and filled where it's new.
static uint32_t shaded_mesh(tide_draw_list *d, const tide_shader *shader, const tide_list vertices,
                            const tide_list indices, const uint32_t count)
{
    const uint64_t key = take_mesh_key(d, vertices, indices);
    if (count == 0 || !shader) return 0;
    const int32_t vertex_count = tide_list_count(vertices);
    const int32_t index_count = tide_list_count(indices);
    if (vertex_count <= 0 || index_count < 3) return 0;
    // As a 3D mesh: its two lists at their versions, and how big a vertex is,
    // so no other kind of list with the same bytes is taken for it
    const uint32_t size = shader->vertex_size;
    uint64_t version, id = MESH_KEYED, other = MESH_KEYED;
    if (key) { // The mesh's key says its vertex size already
        version = key;
    } else {
        version = list_version(vertices, size);
        const uint64_t index_version = list_version(indices, sizeof(uint32_t));
        version = tide_hash_more(version, &index_version, sizeof index_version);
        version = tide_hash_end(tide_hash_more(version, &size, sizeof size));
        const uint64_t scratch = tide_handle_of(TIDE_IN_SCRATCH, 0);
        id = tide_handle_where(vertices.at) == TIDE_IN_SCRATCH ? scratch : vertices.at;
        other = tide_handle_where(indices.at) == TIDE_IN_SCRATCH ? scratch : indices.at;
    }
    version = tide_draw_lod_version(d, version);
    bool fill;
    const uint32_t mesh = tide_draw_mesh_slot(d, TIDE_MESH_BYTES, id, other, version, &fill);
    if (fill) {
        bool vertices_copied, indices_copied;
        void *v = tide_list_flatten(vertices, size, false, &vertices_copied);
        uint32_t *i = tide_list_flatten(indices, sizeof(uint32_t), false, &indices_copied);
        tide_draw_mesh_fill_bytes(d, mesh, v, size, (uint32_t)vertex_count, i, (uint32_t)index_count);
        tide_list_unflatten(indices, i, sizeof(uint32_t), false, indices_copied);
        tide_list_unflatten(vertices, v, size, false, vertices_copied);
        mesh_levels(d, mesh, indices);
        tide_draw_lod_request(d, mesh);
        note_relisted(d, mesh, vertices, indices, d->meshes[mesh - 1u].vertex_size, id, other);
    }
    if (d->diagnostics) {
        d->meshes[mesh - 1u].profile_vertices = vertices.at;
        d->meshes[mesh - 1u].profile_indices = indices.at;
    }
    return mesh;
}

void tide_draw_mesh_shaded_instances(tide_draw_list *d, const tide_shader *shader, const tide_list vertices,
                                     const tide_list indices, const void *instances, const uint32_t count,
                                     const void *uniforms, const uint32_t *textures, const uint64_t sampling)
{
    const uint32_t mesh = shaded_mesh(d, shader, vertices, indices, count);
    if (mesh) tide_draw_shaded_instances(d, shader, mesh, instances, count, uniforms, textures, sampling);
}

static void listed_spans_stale(tide_draw_list *d, uint32_t p);

// The list's entry for an instance list (tide_draw_listed), NULL where none can be had: the table is full of
// lists drawn this frame and the last.
static tide_draw_listed_entry *listed_entry(tide_draw_list *d, const tide_handle at, const uint32_t item_size,
                                            const uint32_t layout)
{
    if (!d->listed) d->listed = tide_alloc_zeroed(1, sizeof *d->listed);
    tide_draw_listed *t = d->listed;
    const uint32_t mask = TIDE_DRAW_LISTED_SLOTS - 1u;
    const uint32_t key[4] = {(uint32_t)at, (uint32_t)(at >> 32), item_size, layout};
    const uint32_t start = (uint32_t)tide_hash(key, sizeof key) & mask;
    for (uint32_t i = start;; i = (i + 1u) & mask) {
        tide_draw_listed_entry *e = &t->entries[i];
        if (e->at == at && e->item_size == item_size && e->layout == layout) return e;
        if (e->at) continue;
        if (t->count < TIDE_DRAW_LISTED_MOST) {
            t->count++;
            // No version yet
            *e = (tide_draw_listed_entry){.at = at, .item_size = item_size, .layout = layout, .since = UINT32_MAX,
                                          .seen = d->frame};
            return e;
        }
        break;
    }
    // Full: the lists no frame asked for since the last go, and the rest are placed again
    listed_spans_stale(d, TIDE_DRAW_LISTED_PAGES);
    tide_draw_listed_entry *old = tide_alloc((size_t)TIDE_DRAW_LISTED_SLOTS * sizeof *old);
    memcpy(old, t->entries, sizeof t->entries);
    memset(t->entries, 0, sizeof t->entries);
    t->count = 0;
    for (uint32_t k = 0; k < TIDE_DRAW_LISTED_SLOTS; k++) {
        const tide_draw_listed_entry *e = &old[k];
        if (!e->at || d->frame - e->seen > 1u) continue;
        const uint32_t again[4] = {(uint32_t)e->at, (uint32_t)(e->at >> 32), e->item_size, e->layout};
        uint32_t i = (uint32_t)tide_hash(again, sizeof again) & mask;
        while (t->entries[i].at) i = (i + 1u) & mask;
        t->entries[i] = *e;
        t->count++;
    }
    free(old);
    if (t->count >= TIDE_DRAW_LISTED_MOST) return NULL;
    return listed_entry(d, at, item_size, layout);
}

// How many frames in a row a list's version changed before it isn't asked for a while, and how long that is.
enum { LISTED_CHANGES = 4, LISTED_WAIT = 16 };

// The list's entry, where the list is as it was the frame before, so its numbers are worth keeping; NULL for one
// made again each frame, which is drawn from the bytes the caller packs, as ever, with only the visible ones sent.
static tide_draw_listed_entry *listed_steady(tide_draw_list *d, const tide_list items, const uint32_t item_size,
                                             const uint32_t layout)
{
    tide_draw_listed_entry *e = listed_entry(d, items.at, item_size, layout);
    if (!e) return NULL;
    const uint32_t frame = d->frame;
    const bool asked_last = e->seen == frame || e->seen + 1u == frame;
    e->seen = frame;
    if (e->wait && frame - e->wait < (uint32_t)LISTED_WAIT) return NULL; // Changed again and again: not asked yet
    const uint64_t version = list_version(items, item_size);
    if (e->since == UINT32_MAX) { // First seen: kept from now, as most lists stay as they are (as if seen before)
        e->version = version;
        e->since = frame - 1u;
        return e;
    }
    if (e->version == version) {
        e->changes = 0;
        e->wait = 0;
        return e->since != frame ? e : NULL;
    }
    e->changes = asked_last && e->since != UINT32_MAX && e->since != frame ? e->changes + 1u : 1u;
    e->version = version;
    e->since = frame;
    e->wait = e->changes >= (uint32_t)LISTED_CHANGES ? frame : 0u;
    return NULL;
}

// The draw list's own texture id for page `p`: no list's (numbers_id's are other hashes), and the top bit clear,
// as a world's numbers have it (a made one's is set).
static uint64_t listed_page_id(const uint32_t p)
{
    return (UINT64_C(0x4C69737465645067) + p) & ~(UINT64_C(1) << 63);
}

enum { LISTED_KIND = 0x4C495354u }; // What writes a page's pixels (tide_draw_texture_edit's kind)

static bool listed_page_kept(const tide_draw_list *d, const tide_draw_listed_page *page)
{
    // A texture no frame drew with is let go at the next reset (tide_draw_reset)
    return page->rows && (page->frame == d->frame || page->frame + 1u == d->frame);
}

// Page `p`'s texture this frame with room for `rows` rows, its pixels written again where `write`: its slot, or 0
// past TIDE_DRAW_LISTED_ROWS. Where its pixels were lost, so are its regions (another generation).
static uint32_t listed_page(tide_draw_list *d, const uint32_t p, const uint32_t rows, const bool write)
{
    tide_draw_listed_page *page = &d->listed->pages[p];
    if (!listed_page_kept(d, page)) {
        listed_spans_stale(d, p);
        page->made++;
        page->rows = 0;
        page->end = 0;
        page->transient_frame = 0;
    }
    // With room past what's asked, for the runs of bytes put among them (tide_draw_listed_put)
    uint32_t want = page->rows ? page->rows : (uint32_t)TIDE_DRAW_LISTED_FIRST_ROWS;
    if (want < rows) {
        const uint32_t asked = rows + rows / 4u + 4u;
        while (want < asked && want < (uint32_t)TIDE_DRAW_LISTED_ROWS) want *= 2u;
    }
    if (want < rows || want > (uint32_t)TIDE_DRAW_LISTED_ROWS) return 0;
    const uint32_t had = page->rows;
    if (write || want != had) page->version++;
    uint8_t *pixels;
    bool kept;
    const uint32_t slot = tide_draw_texture_edit(d, TIDE_PIXELS_LIST, listed_page_id(p), page->version,
                                                 TIDE_LIST_DATA_ACROSS, (int32_t)want, TIDE_TEXELS_INTS, LISTED_KIND,
                                                 &pixels, &kept);
    if (!slot) return 0;
    if (pixels && !kept) { // Made, or grown: its pixels as they were (a texture grows in place), and zeros after
        const size_t row = (size_t)TIDE_LIST_DATA_ACROSS * 16u;
        memset(pixels + (size_t)had * row, 0, (size_t)(want - had) * row);
    }
    page->rows = want;
    page->frame = d->frame;
    return slot;
}

// Whether the frame drew the entry's list, or the frame before: its region is outside the idle budget.
static bool listed_lately(const tide_draw_list *d, const tide_draw_listed_entry *e)
{
    return e->seen == d->frame || e->seen + 1u == d->frame;
}

// Whether the entry's region is where it was written: on its page, in the pixels it was written in.
static bool listed_placed(const tide_draw_list *d, const tide_draw_listed_entry *e)
{
    if (!e->at || !e->page || e->page > TIDE_DRAW_LISTED_PAGES) return false;
    const tide_draw_listed_page *page = &d->listed->pages[e->page - 1u];
    return e->made == page->made && listed_page_kept(d, page);
}

typedef struct listed_idle {
    uint64_t bytes;
    uint32_t age, page, start;
    tide_draw_listed_entry *e;
} listed_idle;

static int listed_idle_order(const void *left, const void *right)
{
    const listed_idle *a = left, *b = right;
    if (a->age != b->age) return a->age < b->age ? -1 : 1;
    if (a->page != b->page) return a->page < b->page ? -1 : 1;
    return a->start < b->start ? -1 : a->start != b->start;
}

// The regions of lists no frame drew lately: those past TIDE_DRAW_LISTED_IDLE_FRAMES go (all of them, where
// `all`), and the rest stay within TIDE_DRAW_LISTED_IDLE_BYTES, the longest unused going first. Which go is the
// list's alone, so it's the same each run.
static void listed_trim(tide_draw_list *d, const bool all)
{
    tide_draw_listed *t = d->listed;
    uint32_t idle = 0;
    uint64_t bytes = 0;
    bool went = false;
    for (uint32_t i = 0; i < TIDE_DRAW_LISTED_SLOTS; i++) {
        tide_draw_listed_entry *e = &t->entries[i];
        if (!e->page) continue;
        if (!listed_placed(d, e)) {
            e->page = 0;
            went = true;
            continue;
        }
        if (listed_lately(d, e)) continue;
        if (all || d->frame - e->seen > (uint32_t)TIDE_DRAW_LISTED_IDLE_FRAMES + 1u) { // Skipped for more frames in a row
            e->page = 0;
            went = true;
            continue;
        }
        idle++;
        bytes += (uint64_t)e->count * e->packs * 16u;
    }
    if (went) listed_spans_stale(d, TIDE_DRAW_LISTED_PAGES);
    if (bytes <= TIDE_DRAW_LISTED_IDLE_BYTES) return;
    listed_spans_stale(d, TIDE_DRAW_LISTED_PAGES);
    listed_idle *order = tide_alloc((size_t)idle * sizeof *order);
    uint32_t count = 0;
    for (uint32_t i = 0; i < TIDE_DRAW_LISTED_SLOTS && count < idle; i++) {
        tide_draw_listed_entry *e = &t->entries[i];
        if (!e->page || listed_lately(d, e)) continue;
        order[count++] = (listed_idle){.bytes = (uint64_t)e->count * e->packs * 16u, .age = d->frame - e->seen,
                                       .page = e->page, .start = e->start, .e = e};
    }
    qsort(order, count, sizeof *order, listed_idle_order);
    uint64_t room = TIDE_DRAW_LISTED_IDLE_BYTES;
    for (uint32_t i = 0; i < count; i++) {
        if (order[i].bytes <= room) room -= order[i].bytes;
        else order[i].e->page = 0;
    }
    free(order);
}

void tide_draw_listed_hold(tide_draw_list *d)
{
    tide_draw_listed *t = d->listed;
    bool held[TIDE_DRAW_LISTED_PAGES] = {false};
    for (uint32_t i = 0; i < TIDE_DRAW_LISTED_SLOTS; i++) {
        const tide_draw_listed_entry *e = &t->entries[i];
        // Still within its frames as the next starts (the budget's bytes are weighed as a frame draws)
        if (listed_placed(d, e) && d->frame - e->seen <= (uint32_t)TIDE_DRAW_LISTED_IDLE_FRAMES) held[e->page - 1u] = true;
    }
    for (uint32_t i = 0; i < d->texture_count; i++) {
        tide_draw_texture *x = &d->textures[i];
        if (x->space != TIDE_PIXELS_LIST) continue;
        for (uint32_t p = 0; p < TIDE_DRAW_LISTED_PAGES; p++) {
            if (x->id != listed_page_id(p)) continue;
            x->held = held[p];
            if (held[p]) t->pages[p].frame = d->frame; // Kept, as though drawn
        }
    }
}

static int listed_span_order(const void *left, const void *right)
{
    const tide_draw_listed_span *a = left, *b = right;
    return a->start < b->start ? -1 : a->start != b->start;
}

void tide_draw_listed_clear(tide_draw_list *d, const bool release)
{
    for (uint32_t p = 0; p < TIDE_DRAW_LISTED_PAGES; p++) free(d->listed->spans[p].spans);
    if (release) {
        free(d->listed);
        d->listed = NULL;
    } else {
        memset(d->listed, 0, sizeof *d->listed);
    }
}

// Page `p`'s regions are to be found again (they moved, or went), or all pages' where `p` is past them.
static void listed_spans_stale(tide_draw_list *d, const uint32_t p)
{
    for (uint32_t i = 0; i < TIDE_DRAW_LISTED_PAGES; i++) {
        if (p >= TIDE_DRAW_LISTED_PAGES || i == p) d->listed->spans[i].frame = 0;
    }
}

// Page `p`'s regions this frame, in order of place: found once, then kept as regions are placed.
static tide_draw_listed_spans *listed_spans(tide_draw_list *d, const uint32_t p)
{
    tide_draw_listed *t = d->listed;
    tide_draw_listed_spans *s = &t->spans[p];
    if (s->frame == d->frame + 1u) return s;
    const tide_draw_listed_page *page = &t->pages[p];
    s->count = 0;
    for (uint32_t i = 0; i < TIDE_DRAW_LISTED_SLOTS; i++) {
        const tide_draw_listed_entry *e = &t->entries[i];
        if (!e->at || e->page != p + 1u || e->made != page->made) continue; // listed_trim kept the rest
        if (s->count == s->capacity) {
            const uint32_t room = s->capacity ? s->capacity * 2u : 64u;
            s->spans = tide_realloc(s->spans, (size_t)s->capacity * sizeof *s->spans, (size_t)room * sizeof *s->spans);
            s->capacity = room;
        }
        s->spans[s->count++] = (tide_draw_listed_span){e->start, e->start + e->count * e->packs};
    }
    if (s->count) qsort(s->spans, s->count, sizeof *s->spans, listed_span_order);
    s->frame = d->frame + 1u;
    return s;
}

// A region placed in page `p` from `start` to `end`, among its spans where they're found already.
static void listed_spans_add(tide_draw_list *d, const uint32_t p, const uint32_t start, const uint32_t end)
{
    tide_draw_listed_spans *s = &d->listed->spans[p];
    if (s->frame != d->frame + 1u) return;
    uint32_t at = s->count;
    while (at && s->spans[at - 1u].start > start) at--;
    if (s->count == s->capacity) {
        const uint32_t room = s->capacity ? s->capacity * 2u : 64u;
        s->spans = tide_realloc(s->spans, (size_t)s->capacity * sizeof *s->spans, (size_t)room * sizeof *s->spans);
        s->capacity = room;
    }
    memmove(s->spans + at + 1u, s->spans + at, (size_t)(s->count - at) * sizeof *s->spans);
    s->spans[at] = (tide_draw_listed_span){start, end};
    s->count++;
}

// ...and one that's let go, where they're found: its room is free again.
static void listed_spans_remove(tide_draw_list *d, const uint32_t p, const uint32_t start)
{
    tide_draw_listed_spans *s = &d->listed->spans[p];
    if (s->frame != d->frame + 1u) return;
    for (uint32_t i = 0; i < s->count; i++) {
        if (s->spans[i].start != start) continue;
        memmove(s->spans + i, s->spans + i + 1u, (size_t)(s->count - i - 1u) * sizeof *s->spans);
        s->count--;
        return;
    }
}

// Where in page `p` `pixels` pixels can start, a multiple of `packs`, past the regions that stay (listed_trim):
// UINT32_MAX for nowhere within TIDE_DRAW_LISTED_ROWS rows.
static uint32_t listed_room(tide_draw_list *d, const uint32_t p, const uint32_t pixels, const uint32_t packs)
{
    tide_draw_listed *t = d->listed;
    const tide_draw_listed_page *page = &t->pages[p];
    const uint64_t most = (uint64_t)TIDE_DRAW_LISTED_ROWS * TIDE_LIST_DATA_ACROSS;
    if (!listed_page_kept(d, page)) return pixels <= most ? 0u : UINT32_MAX; // All of it is free
    // Past them all and the frame's own, as a rule, where the page has room without growing; else in the first
    // gap, or past them
    const bool transient = page->transient_frame == d->frame + 1u;
    const uint32_t end = transient && page->transient_end > page->end ? page->transient_end : page->end;
    const uint64_t after = (end + (uint64_t)packs - 1u) / packs * packs;
    if (after + pixels <= (uint64_t)page->rows * TIDE_LIST_DATA_ACROSS) return (uint32_t)after;
    const tide_draw_listed_spans *s = listed_spans(d, p);
    // The first gap it fits in, or after them all, the frame's own among them
    const tide_draw_listed_span own = {transient ? page->transient_start : 0u, transient ? page->transient_end : 0u};
    bool own_passed = !transient || own.end <= own.start;
    uint64_t at = 0;
    for (uint32_t i = 0;;) {
        at = (at + packs - 1u) / packs * packs;
        tide_draw_listed_span next;
        if (!own_passed && (i == s->count || own.start < s->spans[i].start)) {
            next = own;
            own_passed = true;
        } else if (i < s->count) {
            next = s->spans[i++];
        } else {
            break;
        }
        if (at + pixels <= next.start) break;
        if (next.end > at) at = next.end;
    }
    at = (at + packs - 1u) / packs * packs;
    return at + pixels <= most ? (uint32_t)at : UINT32_MAX;
}

// The page slot of the list's numbers this frame, as they are at its version, written where they weren't: 0
// where there's no room for them.
static uint32_t listed_numbers(tide_draw_list *d, tide_draw_listed_entry *e, const tide_list items,
                               const uint32_t item_size, tide_list_words_fn *write, const uint32_t count,
                               const uint32_t packs)
{
    tide_draw_listed *t = d->listed;
    uint32_t p = e->page ? e->page - 1u : 0u;
    bool here = e->page && e->made == t->pages[p].made && listed_page_kept(d, &t->pages[p]) && e->packs == packs;
    if (here && e->written == e->version && e->count == count) return listed_page(d, p, t->pages[p].rows, false);
    const bool placed = !(here && count <= e->count);
    if (placed) { // Room of its own, on the first page that has it, its own room as it was free
        if (listed_placed(d, e)) listed_spans_remove(d, e->page - 1u, e->start);
        e->page = 0;
        uint32_t start = UINT32_MAX;
        for (p = 0; p < TIDE_DRAW_LISTED_PAGES; p++) {
            start = listed_room(d, p, count * packs, packs);
            if (start != UINT32_MAX) break;
        }
        if (start == UINT32_MAX) { // No room beside the regions that stay: those not drawn lately go, and it's looked for again
            listed_trim(d, true);
            for (p = 0; p < TIDE_DRAW_LISTED_PAGES; p++) {
                start = listed_room(d, p, count * packs, packs);
                if (start != UINT32_MAX) break;
            }
        }
        if (start == UINT32_MAX) {
            e->page = 0;
            return 0;
        }
        e->start = start;
    }
    const uint32_t end = e->start + count * packs;
    const uint32_t rows = (end + (uint32_t)TIDE_LIST_DATA_ACROSS - 1u) / (uint32_t)TIDE_LIST_DATA_ACROSS;
    const uint32_t slot = listed_page(d, p, rows, true);
    if (!slot) {
        e->page = 0;
        return 0;
    }
    uint8_t *to = d->textures[slot - 1u].pixels + (size_t)e->start * 16u;
    bool copied;
    const uint8_t *flat = tide_list_flatten(items, item_size, false, &copied);
    for (uint32_t i = 0; i < count; i++) write(flat + (size_t)i * item_size, (int32_t *)(void *)(to + (size_t)i * packs * 16u));
    tide_list_unflatten(items, (void *)(uintptr_t)flat, item_size, false, copied);
    const uint32_t top = e->start / (uint32_t)TIDE_LIST_DATA_ACROSS;
    tide_draw_texture_changed(d, slot, 0, (int32_t)top, TIDE_LIST_DATA_ACROSS, (int32_t)(rows - top));
    e->page = p + 1u;
    e->made = t->pages[p].made;
    if (end > t->pages[p].end) t->pages[p].end = end;
    if (placed) listed_spans_add(d, p, e->start, end);
    e->count = count;
    e->packs = packs;
    e->written = e->version;
    return slot;
}

// How much room a page's regions can leave between them before they're packed together: half of what they take,
// and this many pixels more (a pixel a pack).
enum { LISTED_SLACK = 65536 };

static int listed_entry_order(const void *left, const void *right)
{
    const tide_draw_listed_entry *a = *(tide_draw_listed_entry *const *)left, *b = *(tide_draw_listed_entry *const *)right;
    return a->start < b->start ? -1 : a->start != b->start;
}

// As a frame's first listed draw starts, before any run of the frame names a place: the regions past the idle
// budget go (listed_trim), and a page whose regions left too much room between them (where lists changed, or a
// list kept its numbers after the frame's own) has them moved down together, its own copy's pixels, and sends
// what moved.
static void listed_pack(tide_draw_list *d)
{
    tide_draw_listed *t = d->listed;
    if (t->packed == d->frame + 1u) return;
    t->packed = d->frame + 1u;
    listed_trim(d, false);
    uint64_t live[TIDE_DRAW_LISTED_PAGES] = {0};
    uint32_t held[TIDE_DRAW_LISTED_PAGES] = {0};
    for (uint32_t i = 0; i < TIDE_DRAW_LISTED_SLOTS; i++) {
        const tide_draw_listed_entry *e = &t->entries[i];
        if (!e->page) continue; // What listed_trim left is placed
        live[e->page - 1u] += (uint64_t)e->count * e->packs;
        held[e->page - 1u]++;
    }
    for (uint32_t p = 0; p < TIDE_DRAW_LISTED_PAGES; p++) {
        tide_draw_listed_page *page = &t->pages[p];
        if (!listed_page_kept(d, page) || page->end <= live[p] + live[p] / 2u + LISTED_SLACK) continue;
        const uint32_t slot = listed_page(d, p, page->rows, true);
        if (!slot) continue;
        tide_draw_listed_entry **order = tide_alloc((size_t)(held[p] ? held[p] : 1u) * sizeof *order);
        uint32_t count = 0;
        for (uint32_t i = 0; i < TIDE_DRAW_LISTED_SLOTS && count < held[p]; i++) {
            tide_draw_listed_entry *e = &t->entries[i];
            if (e->at && e->page == p + 1u) order[count++] = e;
        }
        if (count) qsort(order, count, sizeof *order, listed_entry_order);
        uint8_t *pixels = d->textures[slot - 1u].pixels;
        uint64_t at = 0;
        for (uint32_t i = 0; i < count; i++) { // Each down to the first room after the one before: none is passed
            tide_draw_listed_entry *e = order[i];
            at = (at + e->packs - 1u) / e->packs * e->packs;
            const size_t bytes = (size_t)e->count * e->packs * 16u;
            if (at != e->start) memmove(pixels + (size_t)at * 16u, pixels + (size_t)e->start * 16u, bytes);
            e->start = (uint32_t)at;
            at += (uint64_t)e->count * e->packs;
        }
        free(order);
        listed_spans_stale(d, p);
        page->end = (uint32_t)at;
        page->transient_frame = 0;
        const uint32_t rows = (uint32_t)((at + TIDE_LIST_DATA_ACROSS - 1u) / TIDE_LIST_DATA_ACROSS);
        if (rows) tide_draw_texture_changed(d, slot, 0, 0, TIDE_LIST_DATA_ACROSS, (int32_t)rows);
    }
}

uint32_t tide_draw_listed_put(tide_draw_list *d, const uint32_t slot, const uint8_t *bytes, const uint32_t count,
                              const uint32_t packs)
{
    if (!d->listed || !slot || slot > d->texture_count || !count || !packs) return UINT32_MAX;
    const tide_draw_texture *t = &d->textures[slot - 1u];
    if (t->space != TIDE_PIXELS_LIST) return UINT32_MAX;
    tide_draw_listed *l = d->listed;
    for (uint32_t p = 0; p < TIDE_DRAW_LISTED_PAGES; p++) {
        tide_draw_listed_page *page = &l->pages[p];
        if (t->id != listed_page_id(p) || !listed_page_kept(d, page)) continue;
        const bool going = page->transient_frame == d->frame + 1u;
        const uint32_t after = going && page->transient_end > page->end ? page->transient_end : page->end;
        const uint64_t at = (after + (uint64_t)packs - 1u) / packs * packs;
        if (at + (uint64_t)count * packs > (uint64_t)page->rows * TIDE_LIST_DATA_ACROSS) return UINT32_MAX;
        const uint32_t end = (uint32_t)at + count * packs;
        if (listed_page(d, p, page->rows, true) != slot) return UINT32_MAX;
        if (!going) {
            page->transient_frame = d->frame + 1u;
            page->transient_start = (uint32_t)at;
        }
        page->transient_end = end;
        memcpy(d->textures[slot - 1u].pixels + (size_t)at * 16u, bytes, (size_t)count * packs * 16u);
        const uint32_t top = (uint32_t)at / (uint32_t)TIDE_LIST_DATA_ACROSS;
        const uint32_t rows = (end + (uint32_t)TIDE_LIST_DATA_ACROSS - 1u) / (uint32_t)TIDE_LIST_DATA_ACROSS;
        if (rows == top + 1u) { // Within a row: those pixels alone
            tide_draw_texture_changed(d, slot, (int32_t)(at % TIDE_LIST_DATA_ACROSS), (int32_t)top,
                                      (int32_t)(end - (uint32_t)at), 1);
        } else {
            tide_draw_texture_changed(d, slot, 0, (int32_t)top, TIDE_LIST_DATA_ACROSS, (int32_t)(rows - top));
        }
        return (uint32_t)at;
    }
    return UINT32_MAX;
}

bool tide_draw_mesh_shaded_list(tide_draw_list *d, const tide_shader *shader, const tide_list vertices,
                                const tide_list indices, const tide_list items, const uint32_t item_size,
                                tide_list_words_fn *write, const uint32_t layout, const void *uniforms,
                                const uint32_t *textures, const uint64_t sampling)
{
    // What the renderer draws a listed run with, and what keeps one from being one
    if (!d || !shader || !write || !item_size || shader->dims != 3 || !shader->glsl_listed_vertex
        || shader->texture_count >= TIDE_SHADER_TEXTURES || (d->place.moved & TIDE_DRAW_MOVED_3D)) return false;
    const uint32_t size = tide_shader_instance_size(shader), packs = size / 16u;
    const int32_t count = tide_list_count(items);
    if (size < sizeof(tide_float4x4) || count <= 0
        || (uint64_t)count * packs > (uint64_t)TIDE_DRAW_LISTED_ROWS * TIDE_LIST_DATA_ACROSS) return false;
    if (!d->listed) d->listed = tide_alloc_zeroed(1, sizeof *d->listed);
    listed_pack(d);
    // A world's list that stays as it is keeps its numbers; any other draws its bytes, as ever (where its runs
    // meet listed ones of the same shader and mesh, they're put in the page: tide_draw_listed_put)
    if (!items.at || tide_handle_where(items.at) == TIDE_IN_SCRATCH || !tide_heap_read(tide_handle_where(items.at)))
        return false;
    tide_draw_listed_entry *e = listed_steady(d, items, item_size, layout);
    const uint32_t slot = e ? listed_numbers(d, e, items, item_size, write, (uint32_t)count, packs) : 0u;
    if (!slot) return false;
    const uint32_t start = e->start;
    const uint32_t first = start / packs;
    const uint32_t mesh = shaded_mesh(d, shader, vertices, indices, (uint32_t)count);
    if (mesh) tide_draw_shaded_listed(d, shader, mesh, slot, first, (uint32_t)count, uniforms, textures, sampling);
    return true;
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
