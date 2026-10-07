#include "tide/mesh.h"

#include <string.h>

#include "tide/net.h"
#include "tide/pack.h"

static uint8_t channel(const float v)
{
    if (!(v > 0.0f)) return 0; // Also NaN
    if (v >= 1.0f) return 255;
    return (uint8_t)(v * 255.0f + 0.5f);
}

// The grid's cells as pixels: its chunks' colors, a byte a channel, and clear
// where it has no chunk.
static void grid_pixels(const tide_grid_dir *dir, const tide_heap *heap, const tide_grid_shape *s, uint8_t *pixels)
{
    const int32_t width = dir->size[0], height = dir->size[1];
    memset(pixels, 0, (size_t)width * (size_t)height * 4u);
    const tide_grid_record *records = tide_grid_records(dir);
    const int32_t across = 1 << s->shift[0], down = 1 << s->shift[1];
    for (uint32_t r = 0; r < dir->records; r++) {
        const tide_color *cells = (const tide_color *)(const void *)tide_grid_chunk_at(heap, records[r].block);
        const int32_t x0 = records[r].chunk[0] * across, y0 = records[r].chunk[1] * down;
        for (int32_t y = 0; y < down && y0 + y < height; y++) {
            uint8_t *out = pixels + ((size_t)(y0 + y) * (size_t)width + (size_t)x0) * 4u;
            const tide_color *row = cells + (size_t)y * (size_t)across;
            for (int32_t x = 0; x < across && x0 + x < width; x++, out += 4) {
                out[0] = channel(row[x].r);
                out[1] = channel(row[x].g);
                out[2] = channel(row[x].b);
                out[3] = channel(row[x].a);
            }
        }
    }
}

uint32_t tide_draw_grid_texture(tide_draw_list *d, const tide_grid g, const tide_grid_shape *s)
{
    const tide_grid_dir *dir = tide_grid_dir_of(g);
    // The confirmed world's too: only its pages' hashes are asked of it, which it keeps
    tide_heap *heap = (tide_heap *)(uintptr_t)tide_heap_read(g.at >> 30);
    const bool packed = !heap && tide_pack_holds(g.at); // The pack's (tide/pack.h): its chunks in the scratch area's slots
    if (!dir || (!heap && !packed) || dir->dims != 2u || dir->size[0] <= 0 || dir->size[1] <= 0 || s->cell != sizeof(tide_color)) {
        return 0;
    }
    if (packed) { // Its version is its file's, in its directory's header, as the builder put it
        uint8_t *pixels;
        const uint32_t slot = tide_draw_texture_slot(d, TIDE_PIXELS_GRID, g.at, tide_pack_grid_version(g), dir->size[0], dir->size[1], &pixels);
        if (pixels) grid_pixels(dir, NULL, s, pixels);
        return slot;
    }
    // What its cells are: its size, and each chunk's place and hash, which its
    // heap keeps until the chunk changes
    const bool whole_pages = tide_grid_chunk_bytes(s) == 1u << TIDE_HEAP_PAGE_SHIFT;
    const tide_grid_record *records = tide_grid_records(dir);
    uint64_t version = tide_hash_more(TIDE_HASH_START, dir->size, sizeof dir->size);
    for (uint32_t r = 0; r < dir->records; r++) {
        const uint64_t cells = whole_pages ? tide_heap_block_hash(heap, records[r].block)
                                           : tide_hash(tide_grid_chunk_at(heap, records[r].block), tide_grid_chunk_bytes(s));
        version = tide_hash_more(version, records[r].chunk, sizeof records[r].chunk);
        version = tide_hash_more(version, &cells, sizeof cells);
    }
    uint8_t *pixels;
    const uint32_t slot = tide_draw_texture_slot(d, TIDE_PIXELS_GRID, g.at, tide_hash_end(version), dir->size[0],
                                                 dir->size[1], &pixels);
    if (pixels) grid_pixels(dir, heap, s, pixels);
    return slot;
}

// A filter from any number, as a Tide enum's value can be.
static tide_filter filter_of(const int32_t filter)
{
    return filter == TIDE_FILTER_POINT ? TIDE_FILTER_POINT : TIDE_FILTER_BILINEAR;
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

void tide_draw_mesh_grid_wrapped(tide_draw_list *d, const tide_list vertices, const tide_list indices,
                                 const tide_grid texture, const tide_grid_shape *shape, const int32_t filter,
                                 const int32_t wrap)
{
    mesh_lists(d, vertices, indices, tide_draw_grid_texture(d, texture, shape), filter_of(filter), tide_wrap_of(wrap));
}

void tide_draw_mesh_grid(tide_draw_list *d, const tide_list vertices, const tide_list indices, const tide_grid texture,
                         const tide_grid_shape *shape, const int32_t filter)
{
    tide_draw_mesh_grid_wrapped(d, vertices, indices, texture, shape, filter, TIDE_WRAP_CLAMP);
}

void tide_draw_mesh_texture_wrapped(tide_draw_list *d, const tide_list vertices, const tide_list indices,
                                    const tide_texture_ref texture, const int32_t filter, const int32_t wrap)
{
    mesh_lists(d, vertices, indices, tide_draw_texture_source(d, texture), filter_of(filter), tide_wrap_of(wrap));
}

void tide_draw_mesh_texture(tide_draw_list *d, const tide_list vertices, const tide_list indices,
                            const tide_texture_ref texture, const int32_t filter)
{
    tide_draw_mesh_texture_wrapped(d, vertices, indices, texture, filter, TIDE_WRAP_CLAMP);
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

void tide_draw_mesh3_grid_wrapped(tide_draw_list *d, const tide_list vertices, const tide_list indices,
                                  const tide_float4x4 transform, const tide_grid texture, const tide_grid_shape *shape,
                                  const int32_t filter, const int32_t wrap)
{
    const uint32_t pixels = tide_draw_grid_texture(d, texture, shape);
    tide_draw_instance(d, mesh3_slot(d, vertices, indices), transform, pixels, filter_of(filter), tide_wrap_of(wrap));
}

void tide_draw_mesh3_grid(tide_draw_list *d, const tide_list vertices, const tide_list indices,
                          const tide_float4x4 transform, const tide_grid texture, const tide_grid_shape *shape,
                          const int32_t filter)
{
    tide_draw_mesh3_grid_wrapped(d, vertices, indices, transform, texture, shape, filter, TIDE_WRAP_CLAMP);
}

void tide_draw_mesh3_texture_wrapped(tide_draw_list *d, const tide_list vertices, const tide_list indices,
                                     const tide_float4x4 transform, const tide_texture_ref texture,
                                     const int32_t filter, const int32_t wrap)
{
    tide_draw_instance(d, mesh3_slot(d, vertices, indices), transform, tide_draw_texture_source(d, texture),
                       filter_of(filter), tide_wrap_of(wrap));
}

void tide_draw_mesh3_texture(tide_draw_list *d, const tide_list vertices, const tide_list indices,
                             const tide_float4x4 transform, const tide_texture_ref texture, const int32_t filter)
{
    tide_draw_mesh3_texture_wrapped(d, vertices, indices, transform, texture, filter, TIDE_WRAP_CLAMP);
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

void tide_draw_mesh3_instances_grid_wrapped(tide_draw_list *d, const tide_list vertices, const tide_list indices,
                                            const tide_list transforms, const tide_grid texture,
                                            const tide_grid_shape *shape, const int32_t filter, const int32_t wrap)
{
    if (tide_list_count(transforms) <= 0) return;
    mesh3_instances_lists(d, vertices, indices, transforms, tide_draw_grid_texture(d, texture, shape),
                          filter_of(filter), tide_wrap_of(wrap));
}

void tide_draw_mesh3_instances_grid(tide_draw_list *d, const tide_list vertices, const tide_list indices,
                                   const tide_list transforms, const tide_grid texture, const tide_grid_shape *shape,
                                   const int32_t filter)
{
    tide_draw_mesh3_instances_grid_wrapped(d, vertices, indices, transforms, texture, shape, filter, TIDE_WRAP_CLAMP);
}

void tide_draw_mesh3_instances_texture_wrapped(tide_draw_list *d, const tide_list vertices, const tide_list indices,
                                               const tide_list transforms, const tide_texture_ref texture,
                                               const int32_t filter, const int32_t wrap)
{
    if (tide_list_count(transforms) <= 0) return;
    mesh3_instances_lists(d, vertices, indices, transforms, tide_draw_texture_source(d, texture), filter_of(filter),
                          tide_wrap_of(wrap));
}

void tide_draw_mesh3_instances_texture(tide_draw_list *d, const tide_list vertices, const tide_list indices,
                                       const tide_list transforms, const tide_texture_ref texture, const int32_t filter)
{
    tide_draw_mesh3_instances_texture_wrapped(d, vertices, indices, transforms, texture, filter, TIDE_WRAP_CLAMP);
}
