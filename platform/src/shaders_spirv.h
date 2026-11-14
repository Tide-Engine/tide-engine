#pragma once

// The renderer's shaders (shaders.h) as SPIR-V, for its Vulkan backend: their
// WGSL translated as the platform layer builds (platform/tools/spirv_shaders.c),
// in its desktop builds.

#include <stdint.h>

#define TIDE_BUILTIN_SPIRV(X) X(shape) X(smooth_shape) X(solid_shape) X(mesh) X(text) X(quad) X(text_quad) X(mesh_3d) X(clear) X(clear_all) X(clear_images_0) X(clear_images_1) X(clear_images_2) X(clear_images_3) X(clear_images_4) X(clear_images_5) X(clear_images_6) X(clear_images_7) X(clear_images_8) X(clear_images_9) X(clear_images_10) X(clear_images_11) X(clear_images_12) X(clear_images_13) X(clear_images_14) X(clear_images_15) \
    X(run_size) X(add_offsets) X(add_scatter) X(zero_words) X(hiz_one) X(hiz_samples)

#define TIDE_DECLARE_SPIRV(name)                                                                                       \
    extern const uint32_t tide_spirv_##name[];                                                                         \
    extern const uint32_t tide_spirv_##name##_words;
TIDE_BUILTIN_SPIRV(TIDE_DECLARE_SPIRV)
#undef TIDE_DECLARE_SPIRV
