#pragma once

// The renderer's shaders (shaders.h) as SPIR-V, for its Vulkan backend: their
// WGSL translated as the platform layer builds (platform/tools/spirv_shaders.c),
// in its desktop builds.

#include <stdint.h>

#define TIDE_BUILTIN_SPIRV(X) X(shape) X(smooth_shape) X(solid_shape) X(mesh) X(text) X(quad) X(text_quad) X(mesh_3d) X(clear) X(clear_all)

#define TIDE_DECLARE_SPIRV(name)                                                                                       \
    extern const uint32_t tide_spirv_##name[];                                                                         \
    extern const uint32_t tide_spirv_##name##_words;
TIDE_BUILTIN_SPIRV(TIDE_DECLARE_SPIRV)
#undef TIDE_DECLARE_SPIRV
