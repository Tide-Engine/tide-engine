#include "tide/mesh.h"

#include <string.h>

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
    const uint32_t each = (words + 3u) / 4u; // Pixels an element
    const uint32_t most = (uint32_t)TIDE_LIST_DATA_MOST / each;
    if (most == 0) return 0;
    const uint32_t elements = (uint32_t)held < most ? (uint32_t)held : most;
    const uint32_t pixels = elements * each;
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
        for (uint32_t i = 0; i < elements; i++) write(flat + (size_t)i * element_size, to + (size_t)i * each * 4u);
        tide_list_unflatten(list, (void *)(uintptr_t)flat, element_size, false, copied);
    }
    *count = (int32_t)elements;
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
