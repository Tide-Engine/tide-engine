// WebGPU under the renderer (see src/gpu.h): the page's (tide.js), which has
// each of these as an import. The program draws with it when the page found a
// device before it started (tide_web_init_canvas says); a browser without
// WebGPU has no canvas to draw in, and the page says what it needs.

#include "gpu.h"

#include "tide_web.h"

TIDE_WEB_IMPORT(gpu_epoch) uint32_t tide_web_gpu_epoch(void);
TIDE_WEB_IMPORT(gpu_pass_time_begin) bool tide_web_gpu_pass_time_begin(uint32_t token);
TIDE_WEB_IMPORT(gpu_pass_time_end) void tide_web_gpu_pass_time_end(void);
TIDE_WEB_IMPORT(gpu_pass_time_read) double tide_web_gpu_pass_time_read(uint32_t *token);
TIDE_WEB_IMPORT(gpu_pass_time_reset) void tide_web_gpu_pass_time_reset(void);
TIDE_WEB_IMPORT(gpu_time_begin) void tide_web_gpu_time_begin(void);
TIDE_WEB_IMPORT(gpu_time_end) void tide_web_gpu_time_end(void);
TIDE_WEB_IMPORT(gpu_time_read) double tide_web_gpu_time_read(void);
TIDE_WEB_IMPORT(gpu_time_reset) void tide_web_gpu_time_reset(void);
TIDE_WEB_IMPORT(gpu_buffer) tide_gpu_id tide_web_gpu_buffer(tide_gpu_buffer_kind kind, size_t size, const void *data);
TIDE_WEB_IMPORT(gpu_buffer_write) void tide_web_gpu_buffer_write(tide_gpu_id buffer, size_t offset, const void *data, size_t size);
TIDE_WEB_IMPORT(gpu_buffer_free) void tide_web_gpu_buffer_free(tide_gpu_id buffer);
TIDE_WEB_IMPORT(gpu_texture) tide_gpu_id tide_web_gpu_texture(tide_gpu_format format, int width, int height, const void *pixels);
TIDE_WEB_IMPORT(gpu_texture_blocks) tide_gpu_id tide_web_gpu_texture_blocks(int width, int height, uint32_t format,
                                                                        uint32_t levels, const void *pixels);
TIDE_WEB_IMPORT(gpu_texture_write) void tide_web_gpu_texture_write(tide_gpu_id texture, const void *pixels);
TIDE_WEB_IMPORT(gpu_texture_write_part)
void tide_web_gpu_texture_write_part(tide_gpu_id texture, const void *pixels, int x, int y, int width, int height);
TIDE_WEB_IMPORT(gpu_texture_free) void tide_web_gpu_texture_free(tide_gpu_id texture);
// A cube's faces' pixels are six pointers at `faces`, or it's 0 for none
TIDE_WEB_IMPORT(gpu_cube) tide_gpu_id tide_web_gpu_cube(tide_gpu_color color, int size, const void *const *faces);
TIDE_WEB_IMPORT(gpu_cube_face) tide_gpu_id tide_web_gpu_cube_face(tide_gpu_id cube, uint32_t face);
// A pipeline, piece by piece: its slots and attributes in order, between begin and end
// ...with two eight-slot banks of three-bit tide_gpu_read values.
TIDE_WEB_IMPORT(gpu_pipeline_begin) void tide_web_gpu_pipeline_begin(const char *name, const char *wgsl, uint32_t uniform_size,
                                                             uint32_t texture_count, uint32_t reads, uint32_t reads_high, tide_gpu_blend blend,
                                                             tide_gpu_depth depth, tide_gpu_cull cull,
                                                             tide_gpu_target_format into, uint32_t outputs,
                                                             tide_gpu_stencil_test stencil_test,
                                                             tide_gpu_stencil_op stencil_op, bool alpha_to_coverage);
TIDE_WEB_IMPORT(gpu_pipeline_samplers) void tide_web_gpu_pipeline_samplers(uint32_t count, uint32_t low, uint32_t high);
TIDE_WEB_IMPORT(gpu_pipeline_slot) void tide_web_gpu_pipeline_slot(uint32_t stride, bool per_instance);
TIDE_WEB_IMPORT(gpu_pipeline_attribute) void tide_web_gpu_pipeline_attribute(uint32_t slot, tide_gpu_attribute_format format,
                                                                     uint32_t offset);
// Its storage buffers, a bit of `writes` for each that's written, and whether it's a compute pipeline
TIDE_WEB_IMPORT(gpu_pipeline_storage) void tide_web_gpu_pipeline_storage(uint32_t count, uint32_t writes, bool compute);
// One the renderer does without until it's made (gpu.h, optional): made in the background whatever gpu_background says
TIDE_WEB_IMPORT(gpu_pipeline_optional) void tide_web_gpu_pipeline_optional(void);
TIDE_WEB_IMPORT(gpu_pipeline_end) tide_gpu_id tide_web_gpu_pipeline_end(void);
TIDE_WEB_IMPORT(gpu_pipeline_free) void tide_web_gpu_pipeline_free(tide_gpu_id pipeline);
TIDE_WEB_IMPORT(gpu_background) void tide_web_gpu_background(bool on);
TIDE_WEB_IMPORT(gpu_pipeline_ready) bool tide_web_gpu_pipeline_ready(tide_gpu_id pipeline);
TIDE_WEB_IMPORT(gpu_pipeline_failed) bool tide_web_gpu_pipeline_failed(tide_gpu_id pipeline);
TIDE_WEB_IMPORT(gpu_target) tide_gpu_id tide_web_gpu_target(tide_gpu_target_format format, int width, int height);
TIDE_WEB_IMPORT(gpu_target_limit) int tide_web_gpu_target_limit(void);
TIDE_WEB_IMPORT(gpu_target_texture) tide_gpu_id tide_web_gpu_target_texture(tide_gpu_id target, uint32_t color);
TIDE_WEB_IMPORT(gpu_target_depth) tide_gpu_id tide_web_gpu_target_depth(tide_gpu_id target);
TIDE_WEB_IMPORT(gpu_resolve_depth) void tide_web_gpu_resolve_depth(tide_gpu_id target);
TIDE_WEB_IMPORT(gpu_depth_now) tide_gpu_id tide_web_gpu_depth_now(tide_gpu_id target);
TIDE_WEB_IMPORT(gpu_depth_samples) uint32_t tide_web_gpu_depth_samples(tide_gpu_id target);
TIDE_WEB_IMPORT(gpu_target_free) void tide_web_gpu_target_free(tide_gpu_id target);
TIDE_WEB_IMPORT(gpu_target_read) void tide_web_gpu_target_read(tide_gpu_id target, uint8_t *rgba);
TIDE_WEB_IMPORT(gpu_begin) void tide_web_gpu_begin(tide_gpu_id target, bool clear);
TIDE_WEB_IMPORT(gpu_end) void tide_web_gpu_end(void);
TIDE_WEB_IMPORT(gpu_scissor) void tide_web_gpu_scissor(int x, int y, int width, int height);
TIDE_WEB_IMPORT(gpu_use) void tide_web_gpu_use(tide_gpu_id pipeline);
TIDE_WEB_IMPORT(gpu_stencil) void tide_web_gpu_stencil(uint32_t value);
TIDE_WEB_IMPORT(gpu_uniforms) void tide_web_gpu_uniforms(const void *data, size_t size);
TIDE_WEB_IMPORT(gpu_image) void tide_web_gpu_image(uint32_t slot, tide_gpu_id texture);
TIDE_WEB_IMPORT(gpu_sampler) void tide_web_gpu_sampler(uint32_t slot, tide_gpu_filter filter, tide_gpu_wrap wrap);
TIDE_WEB_IMPORT(gpu_mipmaps) void tide_web_gpu_mipmaps(tide_gpu_id texture);
TIDE_WEB_IMPORT(gpu_vertices) void tide_web_gpu_vertices(uint32_t slot, tide_gpu_id buffer, size_t offset);
TIDE_WEB_IMPORT(gpu_indices) void tide_web_gpu_indices(tide_gpu_id buffer, bool wide);
TIDE_WEB_IMPORT(gpu_draw) void tide_web_gpu_draw(uint32_t first, uint32_t count, uint32_t instances);
TIDE_WEB_IMPORT(gpu_draw_indexed) void tide_web_gpu_draw_indexed(uint32_t count, uint32_t instances);
TIDE_WEB_IMPORT(gpu_storage) void tide_web_gpu_storage(uint32_t slot, tide_gpu_id buffer);
TIDE_WEB_IMPORT(gpu_compute_begin) void tide_web_gpu_compute_begin(void);
TIDE_WEB_IMPORT(gpu_compute_end) void tide_web_gpu_compute_end(void);
TIDE_WEB_IMPORT(gpu_dispatch) void tide_web_gpu_dispatch(uint32_t x, uint32_t y, uint32_t z);
TIDE_WEB_IMPORT(gpu_dispatch_indirect) void tide_web_gpu_dispatch_indirect(tide_gpu_id buffer, size_t offset);
TIDE_WEB_IMPORT(gpu_draw_indirect) void tide_web_gpu_draw_indirect(tide_gpu_id buffer, size_t offset);
TIDE_WEB_IMPORT(gpu_draw_indexed_indirect) void tide_web_gpu_draw_indexed_indirect(tide_gpu_id buffer, size_t offset);
TIDE_WEB_IMPORT(gpu_draw_indexed_indirect_count)
void tide_web_gpu_draw_indexed_indirect_count(tide_gpu_id buffer, size_t offset, uint32_t most, tide_gpu_id counts,
                                              size_t count_offset);
TIDE_WEB_IMPORT(gpu_indirect) uint32_t tide_web_gpu_indirect(void);
TIDE_WEB_IMPORT(gpu_buffer_copy)
void tide_web_gpu_buffer_copy(tide_gpu_id from, size_t from_offset, tide_gpu_id to, size_t to_offset, size_t size);
TIDE_WEB_IMPORT(gpu_buffer_read) uint32_t tide_web_gpu_buffer_read(tide_gpu_id buffer, size_t offset, size_t size);
TIDE_WEB_IMPORT(gpu_buffer_read_done) int tide_web_gpu_buffer_read_done(uint32_t token, void *data);

static tide_gpu_id tide_web_gpu_pipeline(const tide_gpu_pipeline_desc *desc)
{
    uint32_t reads = 0, reads_high = 0;
    for (uint32_t i = 0; i < desc->texture_count && i < TIDE_GPU_MAX_TEXTURES; i++) {
        if (i < 8u) reads |= (uint32_t)desc->reads[i] << (3u * i);
        else reads_high |= (uint32_t)desc->reads[i] << (3u * (i - 8u));
    }
    tide_web_gpu_pipeline_begin(desc->name, desc->wgsl, desc->uniform_size, desc->texture_count, reads, reads_high, desc->blend, desc->depth,
                                desc->cull, desc->into, desc->outputs ? desc->outputs : 1u, desc->stencil_test,
                                desc->stencil_op, desc->alpha_to_coverage);
    if (desc->sampler_count) { // Its samplers, apart from its textures: how each reads, 3 bits each in two banks
        uint8_t sampler_reads[TIDE_GPU_MAX_TEXTURES];
        const uint32_t count = tide_gpu_samplers(desc, sampler_reads);
        uint32_t low = 0, high = 0;
        for (uint32_t j = 0; j < count; j++) {
            if (j < 8u) low |= (uint32_t)sampler_reads[j] << (3u * j);
            else high |= (uint32_t)sampler_reads[j] << (3u * (j - 8u));
        }
        tide_web_gpu_pipeline_samplers(count, low, high);
    }
    for (uint32_t i = 0; i < desc->slot_count; i++) tide_web_gpu_pipeline_slot(desc->slots[i].stride, desc->slots[i].per_instance);
    for (uint32_t i = 0; i < desc->attribute_count; i++) {
        tide_web_gpu_pipeline_attribute(desc->attributes[i].slot, desc->attributes[i].format, desc->attributes[i].offset);
    }
    const uint32_t storage = desc->storage_count < TIDE_GPU_MAX_STORAGE ? desc->storage_count : TIDE_GPU_MAX_STORAGE;
    if (storage || desc->compute) {
        uint32_t writes = 0;
        for (uint32_t i = 0; i < storage; i++) writes |= (desc->compute && desc->storage_writes[i] ? 1u : 0u) << i;
        tide_web_gpu_pipeline_storage(storage, writes, desc->compute);
    }
    if (desc->optional) tide_web_gpu_pipeline_optional();
    return tide_web_gpu_pipeline_end();
}

// WebGPU has two sample counts, 1 and 4, for every format here: what asks for
// more than 1 has 4. A target of depth alone has 1.
// What a target's depth holds now: its texture, or a multisampled one's samples (gpu.h)
static tide_gpu_id tide_web_gpu_depth_now_of(const tide_gpu_id target, uint32_t *samples)
{
    *samples = tide_web_gpu_depth_samples(target);
    return tide_web_gpu_depth_now(target);
}

static uint32_t tide_web_gpu_target_samples(const tide_gpu_target_format format)
{
    return tide_gpu_target_samples(format) > 1u && tide_gpu_target_colors(format) ? 4u : 1u;
}

static tide_gpu_id web_texture_blocks(const int width, const int height, const tide_texture_blocks blocks)
{
    return tide_web_gpu_texture_blocks(width, height, blocks.format, blocks.levels, blocks.pixels);
}

const tide_gpu tide_gpu_webgpu = {
    .name = "WebGPU",
    .epoch = tide_web_gpu_epoch,
    .pass_time_begin = tide_web_gpu_pass_time_begin,
    .pass_time_end = tide_web_gpu_pass_time_end,
    .pass_time_read = tide_web_gpu_pass_time_read,
    .pass_time_reset = tide_web_gpu_pass_time_reset,
    .time_begin = tide_web_gpu_time_begin,
    .time_end = tide_web_gpu_time_end,
    .time_read = tide_web_gpu_time_read,
    .time_reset = tide_web_gpu_time_reset,
    .buffer = tide_web_gpu_buffer,
    .buffer_write = tide_web_gpu_buffer_write,
    .buffer_free = tide_web_gpu_buffer_free,
    .texture = tide_web_gpu_texture,
    .texture_blocks = web_texture_blocks,
    .texture_write = tide_web_gpu_texture_write,
    .texture_write_part = tide_web_gpu_texture_write_part,
    .texture_free = tide_web_gpu_texture_free,
    .cube = tide_web_gpu_cube,
    .cube_face = tide_web_gpu_cube_face,
    .mipmaps = tide_web_gpu_mipmaps,
    .pipeline = tide_web_gpu_pipeline,
    .pipeline_free = tide_web_gpu_pipeline_free,
    .background = tide_web_gpu_background,
    .pipeline_ready = tide_web_gpu_pipeline_ready,
    .pipeline_failed = tide_web_gpu_pipeline_failed,
    .target = tide_web_gpu_target,
    .target_limit = tide_web_gpu_target_limit,
    .target_samples = tide_web_gpu_target_samples,
    .target_texture = tide_web_gpu_target_texture,
    .target_depth = tide_web_gpu_target_depth,
    .resolve_depth = tide_web_gpu_resolve_depth,
    .depth_now = tide_web_gpu_depth_now_of,
    .target_free = tide_web_gpu_target_free,
    .target_read = tide_web_gpu_target_read,
    .begin = tide_web_gpu_begin,
    .end = tide_web_gpu_end,
    .scissor = tide_web_gpu_scissor,
    .use = tide_web_gpu_use,
    .stencil = tide_web_gpu_stencil,
    .uniforms = tide_web_gpu_uniforms,
    .image = tide_web_gpu_image,
    .sampler = tide_web_gpu_sampler,
    .vertices = tide_web_gpu_vertices,
    .indices = tide_web_gpu_indices,
    .draw = tide_web_gpu_draw,
    .draw_indexed = tide_web_gpu_draw_indexed,
    .storage = tide_web_gpu_storage,
    .compute_begin = tide_web_gpu_compute_begin,
    .compute_end = tide_web_gpu_compute_end,
    .dispatch = tide_web_gpu_dispatch,
    .dispatch_indirect = tide_web_gpu_dispatch_indirect,
    .draw_indirect = tide_web_gpu_draw_indirect,
    .draw_indexed_indirect = tide_web_gpu_draw_indexed_indirect,
    .draw_indexed_indirect_count = tide_web_gpu_draw_indexed_indirect_count,
    .indirect = tide_web_gpu_indirect,
    .buffer_copy = tide_web_gpu_buffer_copy,
    .buffer_read = tide_web_gpu_buffer_read,
    .buffer_read_done = tide_web_gpu_buffer_read_done,
    .buffer_read_wait = NULL, // The page's thread can't wait for the GPU
};
