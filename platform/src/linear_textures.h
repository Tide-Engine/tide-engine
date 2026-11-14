// Included by platform.c after gpu and the shader catalogue are available.
// One copy pipeline per source shape; all private destinations use sRGB RGBA8.
static tide_gpu_id linear_copy_pipelines[3];

static void linear_copy_forget(const bool lost)
{
    for (int i = 0; i < 3; i++) {
        if (linear_copy_pipelines[i] && !lost) gpu->pipeline_free(linear_copy_pipelines[i]);
        linear_copy_pipelines[i] = 0;
    }
}

static tide_gpu_id linear_copy_pipeline(const uint32_t read)
{
    const int shape = read == TIDE_GPU_READ_CUBE ? 2 : read == TIDE_GPU_READ_ARRAY ? 1 : 0;
    if (linear_copy_pipelines[shape]) return linear_copy_pipelines[shape];
    const tide_gpu_pipeline_desc desc = {
        .name = shape == 2 ? "linear cube copy" : shape == 1 ? "linear array copy" : "linear texture copy",
        .wgsl = shape == 2 ? linear_cube_wgsl : shape == 1 ? linear_array_wgsl : linear_texture_wgsl,
        .spirv = shape == 2 ? SPIRV_OF(linear_cube) : shape == 1 ? SPIRV_OF(linear_array) : SPIRV_OF(linear_texture),
        .spirv_words = shape == 2 ? SPIRV_WORDS(linear_cube) : shape == 1 ? SPIRV_WORDS(linear_array) : SPIRV_WORDS(linear_texture),
        .uniform_size = 16,
        .texture_count = 1,
        .reads = {(uint8_t)read},
        .blend = TIDE_GPU_BLEND_REPLACE,
        .depth = TIDE_GPU_DEPTH_NONE,
        .into = (tide_gpu_target_format)TIDE_GPU_COLOR_SRGB,
        .outputs = 1,
    };
    return linear_copy_pipelines[shape] = gpu->pipeline(&desc);
}

static void linear_copy_into(const tide_gpu_id dest, const tide_gpu_id source, const uint32_t read, const int layer)
{
    const tide_gpu_id pipeline = linear_copy_pipeline(read);
    const int32_t uniforms[4] = {layer, 0, 0, 0};
    gpu->begin(dest, true);
    if (gpu->depth_bias) gpu->depth_bias(0.0f, 0.0f);
    gpu->use(pipeline);
    gpu->uniforms(uniforms, sizeof uniforms);
    gpu->image(0, source);
    gpu->sampler(0, TIDE_GPU_FILTER_NEAREST, TIDE_GPU_WRAP_CLAMP);
    gpu->draw(0, 3, 1);
    gpu->end();
}
