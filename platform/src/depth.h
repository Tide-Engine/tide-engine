#pragma once

// Private shader convention. Vulkan specializes SpecId 0; WebGPU replaces the constant before compilation.
// Keep the final subtraction here: computing forward depth first loses the distant precision.
#define TIDE_DEPTH_WGSL \
    "const tide_reversed_depth = true;\n" \
    "fn tide_depth(d: f32) -> f32 { return select(d, 1.0 - d, tide_reversed_depth); }\n" \
    "fn tide_clip_depth(p: vec4f) -> f32 { return select((p.z + p.w) * 0.5, (p.w - p.z) * 0.5, tide_reversed_depth); }\n"
