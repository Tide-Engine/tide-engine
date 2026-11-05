// A GPU that draws nothing (tide_gpu_none): for tests whose page runs a game
// that draws but checks nothing drawn (networking, input, files), on a machine
// whose browser gives no WebGPU, as CI's Linux runners don't. Everything is
// taken and kept by number, nothing is drawn, targets read back as zeros and
// so do buffers. A page asks for it with `gpu=none` in its address
// (web/tide.js); no game is ever drawn with it.
#include <string.h>

#include "gpu.h"

static tide_gpu_id next_id;

static tide_gpu_id made(void)
{
    if (++next_id == 0) next_id = 1;
    return next_id;
}

static uint32_t none_epoch(void) { return 1; }

static tide_gpu_id none_buffer(const tide_gpu_buffer_kind kind, const size_t size, const void *data)
{
    (void)kind, (void)size, (void)data;
    return made();
}

static void none_buffer_write(const tide_gpu_id buffer, const size_t offset, const void *data, const size_t size)
{
    (void)buffer, (void)offset, (void)data, (void)size;
}

static void none_free(const tide_gpu_id id) { (void)id; }

static tide_gpu_id none_texture(const tide_gpu_format format, const int width, const int height, const void *pixels)
{
    (void)format, (void)width, (void)height, (void)pixels;
    return made();
}

static tide_gpu_id none_texture_blocks(const int width, const int height, const tide_texture_blocks blocks)
{
    (void)width, (void)height, (void)blocks;
    return 0; // The renderer sends RGBA instead
}

static void none_texture_write(const tide_gpu_id texture, const void *pixels) { (void)texture, (void)pixels; }

static void none_texture_write_part(const tide_gpu_id texture, const void *pixels, const int x, const int y,
                                    const int width, const int height)
{
    (void)texture, (void)pixels, (void)x, (void)y, (void)width, (void)height;
}

static tide_gpu_id none_cube(const tide_gpu_color color, const int size, const void *const *faces)
{
    (void)color, (void)size, (void)faces;
    return made();
}

static tide_gpu_id none_cube_face(const tide_gpu_id cube, const uint32_t face)
{
    (void)cube, (void)face;
    return made();
}

static tide_gpu_id none_array(const tide_gpu_target_format format, const int width, const int height, const uint32_t layers,
                              const void *const *pixels)
{
    (void)format, (void)width, (void)height, (void)layers, (void)pixels;
    return made();
}

static tide_gpu_id none_array_of(const tide_gpu_format format, const int width, const int height, const uint32_t layers)
{
    (void)format, (void)width, (void)height, (void)layers;
    return made();
}

static void none_array_write(const tide_gpu_id array, const uint32_t layer, const void *pixels, const int x, const int y,
                             const int width, const int height)
{
    (void)array, (void)layer, (void)pixels, (void)x, (void)y, (void)width, (void)height;
}

static tide_gpu_id none_array_depth(const tide_gpu_id array)
{
    (void)array;
    return made();
}

static tide_gpu_id none_array_layer(const tide_gpu_id array, const uint32_t layer)
{
    (void)array, (void)layer;
    return made();
}

static tide_gpu_id none_pipeline(const tide_gpu_pipeline_desc *desc)
{
    (void)desc;
    return made();
}

static tide_gpu_id none_target(const tide_gpu_target_format format, const int width, const int height)
{
    (void)format, (void)width, (void)height;
    return made();
}

static int none_target_limit(void) { return 8192; }

static uint32_t none_target_samples(const tide_gpu_target_format format) { return tide_gpu_target_samples(format); }

static tide_gpu_id none_target_texture(const tide_gpu_id target, const uint32_t color)
{
    (void)color;
    return target; // Never sampled for anything
}

static tide_gpu_id none_target_depth(const tide_gpu_id target) { return target; }

static void none_target_read(const tide_gpu_id target, uint8_t *rgba)
{
    (void)target, (void)rgba; // The caller's bytes stay as they are: it gave room for pixels nobody drew
}

static void none_begin(const tide_gpu_id target, const bool clear) { (void)target, (void)clear; }
static void none_nothing(void) { }
static void none_scissor(const int x, const int y, const int width, const int height) { (void)x, (void)y, (void)width, (void)height; }
static void none_stencil(const uint32_t value) { (void)value; }
static void none_uniforms(const void *data, const size_t size) { (void)data, (void)size; }

static void none_image(const uint32_t slot, const tide_gpu_id texture) { (void)slot, (void)texture; }

static void none_sampler(const uint32_t slot, const tide_gpu_filter filter, const tide_gpu_wrap wrap)
{
    (void)slot, (void)filter, (void)wrap;
}

static void none_vertices(const uint32_t slot, const tide_gpu_id buffer, const size_t offset) { (void)slot, (void)buffer, (void)offset; }
static void none_indices(const tide_gpu_id buffer, const bool wide) { (void)buffer, (void)wide; }
static void none_draw(const uint32_t first, const uint32_t count, const uint32_t instances) { (void)first, (void)count, (void)instances; }
static void none_draw_indexed(const uint32_t count, const uint32_t instances) { (void)count, (void)instances; }
static void none_storage(const uint32_t slot, const tide_gpu_id buffer) { (void)slot, (void)buffer; }
static void none_dispatch(const uint32_t x, const uint32_t y, const uint32_t z) { (void)x, (void)y, (void)z; }
static void none_indirect(const tide_gpu_id buffer, const size_t offset) { (void)buffer, (void)offset; }
static void none_indirect_count(const tide_gpu_id buffer, const size_t offset, const uint32_t most, const tide_gpu_id counts,
                                const size_t count_offset)
{
    (void)buffer, (void)offset, (void)most, (void)counts, (void)count_offset;
}
static uint32_t none_indirect_can(void) { return 0; }

static void none_buffer_copy(const tide_gpu_id from, const size_t from_offset, const tide_gpu_id to, const size_t to_offset,
                             const size_t size)
{
    (void)from, (void)from_offset, (void)to, (void)to_offset, (void)size;
}

// Reads back give zeros, at once: each token keeps the size it was asked for
enum { READS = 256 };
static size_t read_sizes[READS];
static uint32_t next_read;

static uint32_t none_buffer_read(const tide_gpu_id buffer, const size_t offset, const size_t size)
{
    (void)buffer, (void)offset;
    next_read = next_read % READS + 1;
    read_sizes[next_read - 1] = size;
    return next_read;
}

static int none_buffer_read_done(const uint32_t token, void *data)
{
    if (token == 0 || token > READS) return -1;
    memset(data, 0, read_sizes[token - 1]);
    return 1;
}

const tide_gpu tide_gpu_none = {
    .name = "none",
    .epoch = none_epoch,
    .buffer = none_buffer,
    .buffer_write = none_buffer_write,
    .buffer_free = none_free,
    .texture = none_texture,
    .texture_blocks = none_texture_blocks,
    .texture_write = none_texture_write,
    .texture_write_part = none_texture_write_part,
    .mipmaps = none_free,
    .texture_free = none_free,
    .cube = none_cube,
    .cube_face = none_cube_face,
    .array = none_array,
    .array_depth = none_array_depth,
    .array_of = none_array_of,
    .array_write = none_array_write,
    .array_layer = none_array_layer,
    .pipeline = none_pipeline,
    .pipeline_free = none_free,
    .target = none_target,
    .target_limit = none_target_limit,
    .target_samples = none_target_samples,
    .target_texture = none_target_texture,
    .target_depth = none_target_depth,
    .resolve_depth = none_free,
    .target_free = none_free,
    .target_read = none_target_read,
    .begin = none_begin,
    .end = none_nothing,
    .scissor = none_scissor,
    .use = none_free,
    .stencil = none_stencil,
    .uniforms = none_uniforms,
    .image = none_image,
    .sampler = none_sampler,
    .vertices = none_vertices,
    .indices = none_indices,
    .draw = none_draw,
    .draw_indexed = none_draw_indexed,
    .storage = none_storage,
    .compute_begin = none_nothing,
    .compute_end = none_nothing,
    .dispatch = none_dispatch,
    .dispatch_indirect = none_indirect,
    .draw_indirect = none_indirect,
    .draw_indexed_indirect = none_indirect,
    .draw_indexed_indirect_count = none_indirect_count,
    .indirect = none_indirect_can,
    .buffer_copy = none_buffer_copy,
    .buffer_read = none_buffer_read,
    .buffer_read_done = none_buffer_read_done,
};
