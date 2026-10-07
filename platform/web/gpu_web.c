// WebGPU under the renderer (see src/gpu.h): the page's (tide.js), which has
// each of these as an import. The program draws with it when the page found a
// device before it started (tide_web_init_canvas says), else with WebGL 2.

#include "gpu.h"

#include "tide_web.h"

TIDE_WEB_IMPORT(gpu_epoch) uint32_t tide_web_gpu_epoch(void);
TIDE_WEB_IMPORT(gpu_time_begin) void tide_web_gpu_time_begin(void);
TIDE_WEB_IMPORT(gpu_time_end) void tide_web_gpu_time_end(void);
TIDE_WEB_IMPORT(gpu_time_read) double tide_web_gpu_time_read(void);
TIDE_WEB_IMPORT(gpu_time_reset) void tide_web_gpu_time_reset(void);
TIDE_WEB_IMPORT(gpu_buffer) tide_gpu_id tide_web_gpu_buffer(tide_gpu_buffer_kind kind, size_t size, const void *data);
TIDE_WEB_IMPORT(gpu_buffer_write) void tide_web_gpu_buffer_write(tide_gpu_id buffer, size_t offset, const void *data, size_t size);
TIDE_WEB_IMPORT(gpu_buffer_free) void tide_web_gpu_buffer_free(tide_gpu_id buffer);
TIDE_WEB_IMPORT(gpu_texture) tide_gpu_id tide_web_gpu_texture(tide_gpu_format format, int width, int height, const void *pixels);
TIDE_WEB_IMPORT(gpu_texture_write) void tide_web_gpu_texture_write(tide_gpu_id texture, const void *pixels);
TIDE_WEB_IMPORT(gpu_texture_free) void tide_web_gpu_texture_free(tide_gpu_id texture);
// A pipeline, piece by piece: its slots and attributes in order, between begin and end
// ...with how it reads its textures, 2 bits each from the lowest: tide_gpu_read
TIDE_WEB_IMPORT(gpu_pipeline_begin) void tide_web_gpu_pipeline_begin(const char *name, const char *wgsl, uint32_t uniform_size,
                                                             uint32_t texture_count, uint32_t reads, tide_gpu_blend blend,
                                                             tide_gpu_depth depth, tide_gpu_cull cull,
                                                             tide_gpu_target_format into, uint32_t outputs,
                                                             tide_gpu_stencil_test stencil_test,
                                                             tide_gpu_stencil_op stencil_op);
TIDE_WEB_IMPORT(gpu_pipeline_slot) void tide_web_gpu_pipeline_slot(uint32_t stride, bool per_instance);
TIDE_WEB_IMPORT(gpu_pipeline_attribute) void tide_web_gpu_pipeline_attribute(uint32_t slot, tide_gpu_attribute_format format,
                                                                     uint32_t offset);
TIDE_WEB_IMPORT(gpu_pipeline_end) tide_gpu_id tide_web_gpu_pipeline_end(void);
TIDE_WEB_IMPORT(gpu_pipeline_free) void tide_web_gpu_pipeline_free(tide_gpu_id pipeline);
TIDE_WEB_IMPORT(gpu_target) tide_gpu_id tide_web_gpu_target(tide_gpu_target_format format, int width, int height);
TIDE_WEB_IMPORT(gpu_target_limit) int tide_web_gpu_target_limit(void);
TIDE_WEB_IMPORT(gpu_target_texture) tide_gpu_id tide_web_gpu_target_texture(tide_gpu_id target, uint32_t color);
TIDE_WEB_IMPORT(gpu_target_depth) tide_gpu_id tide_web_gpu_target_depth(tide_gpu_id target);
TIDE_WEB_IMPORT(gpu_target_free) void tide_web_gpu_target_free(tide_gpu_id target);
TIDE_WEB_IMPORT(gpu_target_read) void tide_web_gpu_target_read(tide_gpu_id target, uint8_t *rgba);
TIDE_WEB_IMPORT(gpu_begin) void tide_web_gpu_begin(tide_gpu_id target, bool clear);
TIDE_WEB_IMPORT(gpu_end) void tide_web_gpu_end(void);
TIDE_WEB_IMPORT(gpu_scissor) void tide_web_gpu_scissor(int x, int y, int width, int height);
TIDE_WEB_IMPORT(gpu_use) void tide_web_gpu_use(tide_gpu_id pipeline);
TIDE_WEB_IMPORT(gpu_stencil) void tide_web_gpu_stencil(uint32_t value);
TIDE_WEB_IMPORT(gpu_uniforms) void tide_web_gpu_uniforms(const void *data, size_t size);
TIDE_WEB_IMPORT(gpu_sample) void tide_web_gpu_sample(uint32_t slot, tide_gpu_id texture, tide_gpu_filter filter, tide_gpu_wrap wrap);
TIDE_WEB_IMPORT(gpu_mipmaps) void tide_web_gpu_mipmaps(tide_gpu_id texture);
TIDE_WEB_IMPORT(gpu_vertices) void tide_web_gpu_vertices(uint32_t slot, tide_gpu_id buffer, size_t offset);
TIDE_WEB_IMPORT(gpu_indices) void tide_web_gpu_indices(tide_gpu_id buffer, bool wide);
TIDE_WEB_IMPORT(gpu_draw) void tide_web_gpu_draw(uint32_t first, uint32_t count, uint32_t instances);
TIDE_WEB_IMPORT(gpu_draw_indexed) void tide_web_gpu_draw_indexed(uint32_t count, uint32_t instances);

static tide_gpu_id tide_web_gpu_pipeline(const tide_gpu_pipeline_desc *desc)
{
    uint32_t reads = 0;
    for (uint32_t i = 0; i < desc->texture_count && i < TIDE_GPU_MAX_TEXTURES; i++) reads |= (uint32_t)desc->reads[i] << (2u * i);
    tide_web_gpu_pipeline_begin(desc->name, desc->wgsl, desc->uniform_size, desc->texture_count, reads, desc->blend, desc->depth,
                                desc->cull, desc->into, desc->outputs ? desc->outputs : 1u, desc->stencil_test,
                                desc->stencil_op);
    for (uint32_t i = 0; i < desc->slot_count; i++) tide_web_gpu_pipeline_slot(desc->slots[i].stride, desc->slots[i].per_instance);
    for (uint32_t i = 0; i < desc->attribute_count; i++) {
        tide_web_gpu_pipeline_attribute(desc->attributes[i].slot, desc->attributes[i].format, desc->attributes[i].offset);
    }
    return tide_web_gpu_pipeline_end();
}

// WebGPU has two sample counts, 1 and 4, for every format here: what asks for
// more than 1 has 4. A target of depth alone has 1.
static uint32_t tide_web_gpu_target_samples(const tide_gpu_target_format format)
{
    return tide_gpu_target_samples(format) > 1u && tide_gpu_target_colors(format) ? 4u : 1u;
}

// Every WebGPU draws into floats of 16 bits, blends them and reads them blended.
static bool tide_web_gpu_float_targets(void)
{
    return true;
}

const tide_gpu tide_gpu_webgpu = {
    .name = "WebGPU",
    .epoch = tide_web_gpu_epoch,
    .time_begin = tide_web_gpu_time_begin,
    .time_end = tide_web_gpu_time_end,
    .time_read = tide_web_gpu_time_read,
    .time_reset = tide_web_gpu_time_reset,
    .buffer = tide_web_gpu_buffer,
    .buffer_write = tide_web_gpu_buffer_write,
    .buffer_free = tide_web_gpu_buffer_free,
    .texture = tide_web_gpu_texture,
    .texture_write = tide_web_gpu_texture_write,
    .texture_free = tide_web_gpu_texture_free,
    .mipmaps = tide_web_gpu_mipmaps,
    .pipeline = tide_web_gpu_pipeline,
    .pipeline_free = tide_web_gpu_pipeline_free,
    .target = tide_web_gpu_target,
    .target_limit = tide_web_gpu_target_limit,
    .target_samples = tide_web_gpu_target_samples,
    .float_targets = tide_web_gpu_float_targets,
    .target_texture = tide_web_gpu_target_texture,
    .target_depth = tide_web_gpu_target_depth,
    .target_free = tide_web_gpu_target_free,
    .target_read = tide_web_gpu_target_read,
    .begin = tide_web_gpu_begin,
    .end = tide_web_gpu_end,
    .scissor = tide_web_gpu_scissor,
    .use = tide_web_gpu_use,
    .stencil = tide_web_gpu_stencil,
    .uniforms = tide_web_gpu_uniforms,
    .sample = tide_web_gpu_sample,
    .vertices = tide_web_gpu_vertices,
    .indices = tide_web_gpu_indices,
    .draw = tide_web_gpu_draw,
    .draw_indexed = tide_web_gpu_draw_indexed,
};
