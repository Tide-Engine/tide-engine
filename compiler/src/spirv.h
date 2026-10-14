#pragma once

// WGSL to SPIR-V, for Vulkan: what tidec writes for a game's frags and verts,
// and the renderer's own shaders, as one module whose entry points are WGSL's
// `vertex` and `fragment`. It takes the WGSL tidec and the renderer write,
// which WebGPU reads too: a module's resources are group 0's, the uniform
// block's binding and each texture's and sampler's, and those keep their
// numbers (descriptor set 0, the same bindings). What's out of range is made
// safe the way WebGPU makes it: integer division and remainder by zero, shifts
// past the width, floats to ints past their range and indices past an
// array's end, so a shader reads the same on both.
//
// SPIR-V 1.3, for Vulkan 1.1 and later.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The module's words, malloc'd (the caller frees them), and how many there
// are; or NULL, having written why into `error` (line and column of the WGSL,
// then what's wrong), for WGSL it doesn't take.
uint32_t *tide_wgsl_to_spirv(const char *wgsl, size_t *count, char *error, size_t error_size);
