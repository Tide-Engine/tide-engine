// gpu.h's backend inside another program (tide/embed.h): nothing is drawn
// here. Each call is written down as tide_embed_render's commands say, for
// that program to replay with its own GPU API (Unity's TideRender.cs). An
// object is a number this backend gives out, from 1, which the replay maps to
// its own; a target's textures get theirs as it's made. A pipeline is known
// by its shaders: a hash of its SPIR-V, or of its WGSL where it has none,
// which is what tide unity names the shader it wrote for it.

#include <stdlib.h>
#include <string.h>

#include "tide/embed.h"
#include "tide/page.h"
#include "gpu.h"

static uint8_t *stream;
static size_t stream_size, stream_capacity;
static uint32_t next_id;
static uint32_t epoch = 1;

static void room(const size_t bytes)
{
    if (stream_size + bytes <= stream_capacity) return;
    size_t capacity = stream_capacity ? stream_capacity : 65536u;
    while (capacity < stream_size + bytes) capacity *= 2u;
    stream = tide_realloc(stream, stream_capacity, capacity);
    stream_capacity = capacity;
}

// A command: its op, and how many bytes follow, a multiple of 4.
static void op(const tide_embed_render_op o, const size_t bytes)
{
    room(8u + ((bytes + 3u) & ~(size_t)3u));
    const uint32_t head[2] = {(uint32_t)o, (uint32_t)((bytes + 3u) & ~(size_t)3u)};
    memcpy(stream + stream_size, head, sizeof head);
    stream_size += sizeof head;
}

static void u32(const uint32_t v)
{
    memcpy(stream + stream_size, &v, 4u);
    stream_size += 4u;
}

// Bytes, and zeros to the next multiple of 4 (or `count` zeros for no bytes).
static void bytes(const void *data, const size_t count)
{
    if (data) memcpy(stream + stream_size, data, count);
    else memset(stream + stream_size, 0, count);
    const size_t padded = (count + 3u) & ~(size_t)3u;
    memset(stream + stream_size + count, 0, padded - count);
    stream_size += padded;
}

static uint32_t record_epoch(void)
{
    return epoch;
}

static tide_gpu_id record_buffer(const tide_gpu_buffer_kind kind, const size_t size, const void *data)
{
    const uint32_t id = ++next_id;
    op(TIDE_EMBED_RENDER_BUFFER, 16u + (data ? size : 0u));
    u32(id);
    u32((uint32_t)kind);
    u32((uint32_t)size);
    u32(data != NULL);
    if (data) bytes(data, size);
    return id;
}

static void record_buffer_write(const tide_gpu_id buffer, const size_t offset, const void *data, const size_t size)
{
    op(TIDE_EMBED_RENDER_BUFFER_WRITE, 12u + size);
    u32(buffer);
    u32((uint32_t)offset);
    u32((uint32_t)size);
    bytes(data, size);
}

static void freed(const tide_embed_render_op o, const tide_gpu_id id)
{
    if (!id) return;
    op(o, 4u);
    u32(id);
}

static void record_buffer_free(const tide_gpu_id buffer)
{
    freed(TIDE_EMBED_RENDER_BUFFER_FREE, buffer);
}

static tide_gpu_id record_texture(const tide_gpu_format format, const int width, const int height, const void *pixels)
{
    const uint32_t id = ++next_id;
    const size_t size = pixels ? (size_t)width * (size_t)height * (size_t)tide_gpu_format_bytes(format) : 0u;
    op(TIDE_EMBED_RENDER_TEXTURE, 20u + size);
    u32(id);
    u32((uint32_t)format);
    u32((uint32_t)width);
    u32((uint32_t)height);
    u32(pixels != NULL);
    if (pixels) bytes(pixels, size);
    return id;
}

static tide_gpu_id record_texture_blocks(const int width, const int height, const tide_texture_blocks blocks)
{
    (void)width, (void)height, (void)blocks;
    return 0; // The renderer sends RGBA instead
}

// A texture's part, its rows one after another: `pixels` is the whole texture's.
static void write_part(const tide_gpu_id texture, const tide_gpu_format format, const int texture_width, const void *pixels,
                       const int x, const int y, const int width, const int height)
{
    const size_t pixel = (size_t)tide_gpu_format_bytes(format), row = (size_t)width * pixel;
    op(TIDE_EMBED_RENDER_TEXTURE_WRITE, 20u + row * (size_t)height);
    u32(texture);
    u32((uint32_t)x);
    u32((uint32_t)y);
    u32((uint32_t)width);
    u32((uint32_t)height);
    for (int r = 0; r < height; r++) {
        memcpy(stream + stream_size, (const uint8_t *)pixels + ((size_t)(y + r) * (size_t)texture_width + (size_t)x) * pixel, row);
        stream_size += row;
    }
    bytes(NULL, ((row * (size_t)height + 3u) & ~(size_t)3u) - row * (size_t)height);
}

// What the textures written to are, as the replay needs to cut their parts: their format and width.
typedef struct texture_shape {
    tide_gpu_id id;
    tide_gpu_format format;
    int width, height;
} texture_shape;

static texture_shape *shapes;
static uint32_t shape_count, shape_capacity;

static texture_shape *shape_of(const tide_gpu_id id)
{
    for (uint32_t i = 0; i < shape_count; i++) {
        if (shapes[i].id == id) return &shapes[i];
    }
    return NULL;
}

static tide_gpu_id record_texture_shaped(const tide_gpu_format format, const int width, const int height, const void *pixels)
{
    const tide_gpu_id id = record_texture(format, width, height, pixels);
    if (shape_count == shape_capacity) {
        const uint32_t capacity = shape_capacity ? shape_capacity * 2u : 64u;
        shapes = tide_realloc(shapes, shape_capacity * sizeof(texture_shape), capacity * sizeof(texture_shape));
        shape_capacity = capacity;
    }
    shapes[shape_count++] = (texture_shape){id, format, width, height};
    return id;
}

static void record_texture_write(const tide_gpu_id texture, const void *pixels)
{
    const texture_shape *s = shape_of(texture);
    if (s) write_part(texture, s->format, s->width, pixels, 0, 0, s->width, s->height);
}

static void record_texture_write_part(const tide_gpu_id texture, const void *pixels, const int x, const int y, const int width,
                                      const int height)
{
    const texture_shape *s = shape_of(texture);
    if (s) write_part(texture, s->format, s->width, pixels, x, y, width, height);
}

static void record_mipmaps(const tide_gpu_id texture)
{
    op(TIDE_EMBED_RENDER_MIPMAPS, 4u);
    u32(texture);
}

static void record_texture_free(const tide_gpu_id texture)
{
    for (uint32_t i = 0; i < shape_count; i++) {
        if (shapes[i].id == texture) shapes[i] = shapes[--shape_count];
    }
    freed(TIDE_EMBED_RENDER_TEXTURE_FREE, texture);
}

static tide_gpu_id record_cube(const tide_gpu_color color, const int size, const void *const *faces)
{
    const uint32_t id = ++next_id;
    const size_t face = (size_t)size * (size_t)size * 4u;
    op(TIDE_EMBED_RENDER_CUBE, 16u + (faces ? face * TIDE_GPU_CUBE_FACES : 0u));
    u32(id);
    u32((uint32_t)color);
    u32((uint32_t)size);
    u32(faces != NULL);
    for (int i = 0; faces && i < TIDE_GPU_CUBE_FACES; i++) bytes(faces[i], face);
    return id;
}

// A target's textures, by their target: its colors' and its depth's.
typedef struct target_textures {
    tide_gpu_id target;
    tide_gpu_id colors[TIDE_GPU_MAX_COLORS];
    tide_gpu_id depth;
    bool multisampled;
} target_textures;

static target_textures *targets;
static uint32_t target_count, target_capacity;

static target_textures *add_target(const tide_gpu_id id)
{
    if (target_count == target_capacity) {
        const uint32_t capacity = target_capacity ? target_capacity * 2u : 32u;
        targets = tide_realloc(targets, target_capacity * sizeof(target_textures), capacity * sizeof(target_textures));
        target_capacity = capacity;
    }
    target_textures *t = &targets[target_count++];
    *t = (target_textures){.target = id};
    return t;
}

static target_textures *find_target(const tide_gpu_id id)
{
    for (uint32_t i = 0; i < target_count; i++) {
        if (targets[i].target == id) return &targets[i];
    }
    return NULL;
}

static tide_gpu_id record_cube_face(const tide_gpu_id cube, const uint32_t face)
{
    const uint32_t id = ++next_id;
    target_textures *t = add_target(id);
    t->depth = ++next_id;
    op(TIDE_EMBED_RENDER_CUBE_FACE, 16u);
    u32(id);
    u32(cube);
    u32(face);
    u32(t->depth);
    return id;
}

// FNV-1a's 64 bits, as tide unity names a shader by.
static uint64_t hash(const void *data, const size_t size)
{
    uint64_t h = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < size; i++) {
        h ^= ((const uint8_t *)data)[i];
        h *= UINT64_C(1099511628211);
    }
    return h;
}

static tide_gpu_id record_pipeline(const tide_gpu_pipeline_desc *desc)
{
    const uint32_t id = ++next_id;
    const uint64_t key = desc->spirv ? hash(desc->spirv, (size_t)desc->spirv_words * 4u)
                       : desc->wgsl ? hash(desc->wgsl, strlen(desc->wgsl)) : 0u;
    const char *name = desc->name ? desc->name : "";
    const size_t name_size = strlen(name);
    const uint32_t textures = desc->texture_count < TIDE_GPU_MAX_TEXTURES ? desc->texture_count : TIDE_GPU_MAX_TEXTURES;
    op(TIDE_EMBED_RENDER_PIPELINE, 4u * (3u + 1u + 2u * TIDE_GPU_MAX_SLOTS + 1u + 3u * TIDE_GPU_MAX_ATTRIBUTES + 2u
                                         + textures + 8u + 1u) + name_size);
    u32(id);
    u32((uint32_t)key);
    u32((uint32_t)(key >> 32));
    u32(desc->slot_count);
    for (uint32_t i = 0; i < TIDE_GPU_MAX_SLOTS; i++) {
        u32(i < desc->slot_count ? desc->slots[i].stride : 0u);
        u32(i < desc->slot_count && desc->slots[i].per_instance);
    }
    u32(desc->attribute_count);
    for (uint32_t i = 0; i < TIDE_GPU_MAX_ATTRIBUTES; i++) {
        const bool used = i < desc->attribute_count;
        u32(used ? desc->attributes[i].slot : 0u);
        u32(used ? (uint32_t)desc->attributes[i].format : 0u);
        u32(used ? desc->attributes[i].offset : 0u);
    }
    u32(desc->uniform_size);
    u32(textures);
    for (uint32_t i = 0; i < textures; i++) u32(desc->reads[i]);
    u32((uint32_t)desc->blend);
    u32((uint32_t)desc->depth);
    u32((uint32_t)desc->cull);
    u32((uint32_t)desc->stencil_test);
    u32((uint32_t)desc->stencil_op);
    u32(desc->into);
    u32(desc->outputs);
    u32(desc->alpha_to_coverage);
    u32((uint32_t)name_size);
    bytes(name, name_size);
    return id;
}

static void record_pipeline_free(const tide_gpu_id pipeline)
{
    freed(TIDE_EMBED_RENDER_PIPELINE_FREE, pipeline);
}

static tide_gpu_id record_target(const tide_gpu_target_format format, const int width, const int height)
{
    const uint32_t id = ++next_id;
    target_textures *t = add_target(id);
    const uint32_t colors = tide_gpu_target_colors(format);
    for (uint32_t i = 0; i < colors; i++) t->colors[i] = ++next_id;
    t->multisampled = tide_gpu_target_samples(format) > 1u;
    if (!t->multisampled) t->depth = ++next_id;
    op(TIDE_EMBED_RENDER_TARGET, 4u * (4u + TIDE_GPU_MAX_COLORS + 1u));
    u32(id);
    u32(format);
    u32((uint32_t)width);
    u32((uint32_t)height);
    for (uint32_t i = 0; i < TIDE_GPU_MAX_COLORS; i++) u32(t->colors[i]);
    u32(t->depth);
    return id;
}

static int record_target_limit(void)
{
    return 16384;
}

static uint32_t record_target_samples(const tide_gpu_target_format format)
{
    const uint32_t asked = tide_gpu_target_samples(format);
    return asked >= 8u ? 8u : asked >= 4u ? 4u : asked >= 2u ? 2u : 1u;
}

static bool record_float_targets(void)
{
    return true;
}

static tide_gpu_id record_target_texture(const tide_gpu_id target, const uint32_t color)
{
    const target_textures *t = find_target(target);
    return t && color < TIDE_GPU_MAX_COLORS ? t->colors[color] : 0u;
}

static tide_gpu_id record_target_depth(const tide_gpu_id target)
{
    const target_textures *t = find_target(target);
    return t ? t->depth : 0u;
}

static void record_resolve_depth(const tide_gpu_id target)
{
    target_textures *t = find_target(target);
    if (!t || !t->multisampled) return;
    if (!t->depth) t->depth = ++next_id;
    op(TIDE_EMBED_RENDER_RESOLVE_DEPTH, 8u);
    u32(target);
    u32(t->depth);
}

static void record_target_free(const tide_gpu_id target)
{
    for (uint32_t i = 0; i < target_count; i++) {
        if (targets[i].target == target) targets[i] = targets[--target_count];
    }
    freed(TIDE_EMBED_RENDER_TARGET_FREE, target);
}

static void record_target_read(const tide_gpu_id target, uint8_t *rgba)
{
    // Drawn later, by the other program: nothing to read yet
    (void)target, (void)rgba;
}

static void record_begin(const tide_gpu_id target, const bool clear)
{
    op(TIDE_EMBED_RENDER_BEGIN, 8u);
    u32(target);
    u32(clear);
}

static void record_end(void)
{
    op(TIDE_EMBED_RENDER_END, 0u);
}

static void record_scissor(const int x, const int y, const int width, const int height)
{
    op(TIDE_EMBED_RENDER_SCISSOR, 16u);
    u32((uint32_t)x);
    u32((uint32_t)y);
    u32((uint32_t)width);
    u32((uint32_t)height);
}

static void record_use(const tide_gpu_id pipeline)
{
    op(TIDE_EMBED_RENDER_USE, 4u);
    u32(pipeline);
}

static void record_stencil(const uint32_t value)
{
    op(TIDE_EMBED_RENDER_STENCIL, 4u);
    u32(value);
}

static void record_uniforms(const void *data, const size_t size)
{
    op(TIDE_EMBED_RENDER_UNIFORMS, 4u + size);
    u32((uint32_t)size);
    bytes(data, size);
}

static void record_sample(const uint32_t slot, const tide_gpu_id texture, const tide_gpu_filter filter, const tide_gpu_wrap wrap)
{
    op(TIDE_EMBED_RENDER_SAMPLE, 16u);
    u32(slot);
    u32(texture);
    u32((uint32_t)filter);
    u32((uint32_t)wrap);
}

static void record_vertices(const uint32_t slot, const tide_gpu_id buffer, const size_t offset)
{
    op(TIDE_EMBED_RENDER_VERTICES, 12u);
    u32(slot);
    u32(buffer);
    u32((uint32_t)offset);
}

static void record_indices(const tide_gpu_id buffer, const bool wide)
{
    op(TIDE_EMBED_RENDER_INDICES, 8u);
    u32(buffer);
    u32(wide);
}

static void record_draw(const uint32_t first, const uint32_t count, const uint32_t instances)
{
    op(TIDE_EMBED_RENDER_DRAW, 12u);
    u32(first);
    u32(count);
    u32(instances);
}

static void record_draw_indexed(const uint32_t count, const uint32_t instances)
{
    op(TIDE_EMBED_RENDER_DRAW_INDEXED, 8u);
    u32(count);
    u32(instances);
}

const tide_gpu tide_gpu_record = {
    .name = "Unity",
    .epoch = record_epoch,
    .buffer = record_buffer,
    .buffer_write = record_buffer_write,
    .buffer_free = record_buffer_free,
    .texture = record_texture_shaped,
    .texture_blocks = record_texture_blocks,
    .texture_write = record_texture_write,
    .texture_write_part = record_texture_write_part,
    .mipmaps = record_mipmaps,
    .texture_free = record_texture_free,
    .cube = record_cube,
    .cube_face = record_cube_face,
    .pipeline = record_pipeline,
    .pipeline_free = record_pipeline_free,
    .target = record_target,
    .target_limit = record_target_limit,
    .target_samples = record_target_samples,
    .float_targets = record_float_targets,
    .target_texture = record_target_texture,
    .target_depth = record_target_depth,
    .resolve_depth = record_resolve_depth,
    .target_free = record_target_free,
    .target_read = record_target_read,
    .begin = record_begin,
    .end = record_end,
    .scissor = record_scissor,
    .use = record_use,
    .stencil = record_stencil,
    .uniforms = record_uniforms,
    .sample = record_sample,
    .vertices = record_vertices,
    .indices = record_indices,
    .draw = record_draw,
    .draw_indexed = record_draw_indexed,
};

void tide_gpu_record_begin(void)
{
    stream_size = 0;
}

const uint8_t *tide_gpu_record_end(uint32_t *size)
{
    *size = (uint32_t)stream_size;
    return stream;
}

void tide_gpu_record_reset(void)
{
    epoch++;
    next_id = 0;
    shape_count = 0;
    target_count = 0;
}

void tide_gpu_record_close(void)
{
    free(stream);
    stream = NULL;
    stream_size = stream_capacity = 0;
    free(shapes);
    shapes = NULL;
    shape_count = shape_capacity = 0;
    free(targets);
    targets = NULL;
    target_count = target_capacity = 0;
    epoch++;
    next_id = 0;
}
